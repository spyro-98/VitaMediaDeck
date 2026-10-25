#include "ui/theme_scene.h"
#include "settings/preferences.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include <math.h>
#include <stdlib.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <string.h>

/* Each quad is transformed in model space, sorted back-to-front and projected.
 * GXM consumes one bounded triangle batch from its mapped per-frame pool.
 * No FBO, texture upload, shader compilation or heap allocation in this pass. */
typedef struct {
    float x, y, z;
} SceneVec;
typedef struct {
    SceneVec v[4];
    float depth;
    unsigned color;
} SceneFace;
#define SCENE_FACES 192
static SceneFace scene_faces[SCENE_FACES];
static uint16_t scene_order[SCENE_FACES];
static vita2d_color_vertex scene_vertices[SCENE_FACES * 6];
static unsigned scene_count;
static float scene_x, scene_y, scene_scale, scene_opacity;
static struct {
    uint64_t hash, changed, last, clock;
    int theme, ready;
    UiPageMotion page;
    UiPageView view;
} scene_state[UI_SCENE_COUNT];
static void scene_picture(vita2d_texture *art,float x,float y,float size,float pitch,float yaw,float roll);
static unsigned scene_alpha(unsigned c, float opacity) {
    if (opacity < 0)
        opacity = 0;
    if (opacity > 1)
        opacity = 1;
    return (c & 0xffffffu) | ((unsigned)(((c >> 24) & 255) * opacity) << 24);
}
static unsigned scene_tint(unsigned a, unsigned b, float f) {
    if (f < 0)
        f = 0;
    if (f > 1)
        f = 1;
    return RGBA8((unsigned)((a & 255) * (1 - f) + (b & 255) * f),
                 (unsigned)(((a >> 8) & 255) * (1 - f) + ((b >> 8) & 255) * f),
                 (unsigned)(((a >> 16) & 255) * (1 - f) + ((b >> 16) & 255) * f), 255);
}
static SceneVec scene_rotate(SceneVec v, float ax, float ay, float az) {
    float sx = sinf(ax), cx = cosf(ax), sy = sinf(ay), cy = cosf(ay), sz = sinf(az), cz = cosf(az);
    SceneVec a = {v.x, v.y * cx - v.z * sx, v.y * sx + v.z * cx};
    SceneVec b = {a.x * cy + a.z * sy, a.y, -a.x * sy + a.z * cy};
    return (SceneVec){b.x * cz - b.y * sz, b.x * sz + b.y * cz, b.z};
}
static void scene_quad(SceneVec a, SceneVec b, SceneVec c, SceneVec d, unsigned color) {
    if (scene_count >= SCENE_FACES)
        return;
    SceneFace f = {{a, b, c, d}, (a.z + b.z + c.z + d.z) * .25f, scene_alpha(color, scene_opacity)};
    /* Faces are small/bounded; stable insertion sort avoids an allocator/qsort. */
    unsigned index = scene_count++, at = index;
    scene_faces[index] = f;
    while (at && scene_faces[scene_order[at - 1]].depth < f.depth) {
        scene_order[at] = scene_order[at - 1];
        at--;
    }
    scene_order[at] = (uint16_t)index;
}
static void scene_box(float x, float y, float z, float w, float h, float depth, float ax, float ay,
                      float az, unsigned color) {
    SceneVec v[8];
    float sx = sinf(ax), cx = cosf(ax), sy = sinf(ay), cy = cosf(ay), sz = sinf(az), cz = cosf(az);
    for (int i = 0; i < 8; i++) {
        SceneVec p = {(i & 1 ? 1 : -1) * w * .5f, (i & 2 ? 1 : -1) * h * .5f,
                      (i & 4 ? 1 : -1) * depth * .5f};
        SceneVec a = {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};
        SceneVec b = {a.x * cy + a.z * sy, a.y, -a.x * sy + a.z * cy};
        p = (SceneVec){b.x * cz - b.y * sz, b.x * sz + b.y * cz, b.z};
        p.x += x;
        p.y += y;
        p.z += z;
        v[i] = p;
    }
    static const unsigned char indices[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                                                {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (int i = 0; i < 6; i++) {
        SceneVec a = v[indices[i][0]], b = v[indices[i][1]], c = v[indices[i][2]],
                 d = v[indices[i][3]];
        SceneVec u = {b.x - a.x, b.y - a.y, b.z - a.z}, t = {c.x - a.x, c.y - a.y, c.z - a.z};
        SceneVec n = {u.y * t.z - u.z * t.y, u.z * t.x - u.x * t.z, u.x * t.y - u.y * t.x};
        float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < .001f)
            continue;
        /* No back-face culling: translucent bevel faces deliberately show thickness. */
        float light = fabsf((-.35f * n.x - .55f * n.y - .76f * n.z) / len);
        unsigned lit = scene_tint(VT_THEME_BG, color, .23f + .77f * light);
        scene_quad(a, b, c, d, scene_alpha(lit, (color >> 24) / 255.0f));
    }
}
static void scene_ring(float radius, float thickness, float ax, float ay, float angle,
                       unsigned color) {
    const int n = 48;
    static float circle[49][2];
    static int ready;
    if (!ready) {
        for (int i = 0; i <= 48; i++) {
            float a = i * 6.28318530718f / 48;
            circle[i][0] = cosf(a);
            circle[i][1] = sinf(a);
        }
        ready = 1;
    }
    float sx = sinf(ax), cx = cosf(ax), sy = sinf(ay), cy = cosf(ay), sz = sinf(angle),
          cz = cosf(angle);
    for (int i = 0; i < n; i++) {
        if (i % 12 == 11)
            continue;
        SceneVec points[4] = {
            {circle[i][0] * radius, circle[i][1] * radius, 0},
            {circle[i + 1][0] * radius, circle[i + 1][1] * radius, 0},
            {circle[i + 1][0] * (radius - thickness), circle[i + 1][1] * (radius - thickness), 0},
            {circle[i][0] * (radius - thickness), circle[i][1] * (radius - thickness), 0}};
        for (int j = 0; j < 4; j++) {
            SceneVec p = points[j], q = {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};
            SceneVec t = {q.x * cy + q.z * sy, q.y, -q.x * sy + q.z * cy};
            points[j] = (SceneVec){t.x * cz - t.y * sz, t.x * sz + t.y * cz, t.z};
        }
        scene_quad(points[0], points[1], points[2], points[3], color);
    }
}
static void scene_flush(void) {
    unsigned count = 0;
    static const unsigned char tris[6] = {0, 1, 2, 0, 2, 3};
    for (unsigned i = 0; i < scene_count; i++)
        for (int j = 0; j < 6; j++) {
            SceneVec p = scene_faces[scene_order[i]].v[tris[j]];
            float denominator = 600 + p.z * scene_scale;
            if (denominator < 100)
                denominator = 100;
            float s = scene_scale * 600 / denominator;
            scene_vertices[count++] = (vita2d_color_vertex){scene_x + p.x * s, scene_y + p.y * s,
                                                            .5f, scene_faces[scene_order[i]].color};
        }
    if (count) {
#ifdef __vita__
        unsigned bytes = count * sizeof(vita2d_color_vertex);
        vita2d_color_vertex *gpu = vita2d_pool_memalign(bytes, 16);
        if (gpu) {
            memcpy(gpu, scene_vertices, bytes);
            vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, gpu, count);
        }
#else
        vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, scene_vertices, count);
#endif
    }
    scene_count = 0;
}
UiPageView ui_theme_page(UiThemeScene scene, const char *owner, const char *identity,
    int first, int count, int columns, int slots, float step) {
    if(scene<0||scene>=UI_SCENE_COUNT)return (UiPageView){0};
    scene_state[scene].view=ui_page_update(&scene_state[scene].page,
        sceKernelGetProcessTimeWide(),ui_theme_id(),vt_preferences_reduce_motion(),
        owner,identity,first,count,columns,slots,step);
    return scene_state[scene].view;
}
void ui_theme_page_rebase(UiThemeScene scene,int before,int after) {
    if(scene>=0&&scene<UI_SCENE_COUNT)ui_page_rebase(&scene_state[scene].page,before,after);
}
UiThemeRect ui_theme_page_rect(UiThemeScene scene, UiThemeRect r) {
    if(scene<0||scene>=UI_SCENE_COUNT)return r;
    /* A hidden screen must not leak its last offset into a new hit test. */
    UiPageMotion *m=&scene_state[scene].page;
    uint64_t now=sceKernelGetProcessTimeWide();
    if(!m->ready||m->theme!=ui_theme_id()||vt_preferences_reduce_motion()||
       now<m->last||now-m->last>250000ULL)return r;
    r.x+=(int)roundf(scene_state[scene].view.dx);
    r.y+=(int)roundf(scene_state[scene].view.dy);
    return r;
}
void ui_theme_page_finish(UiThemeScene scene,int x,int top,int width,int bottom) {
    if(scene<0||scene>=UI_SCENE_COUNT||bottom<=top||width<20||vt_preferences_reduce_motion())return;
    float energy=scene_state[scene].view.energy;
    if(energy<.005f)return;
    int theme=ui_theme_id();
    unsigned color=(VT_THEME_FOCUS&0xffffffu)|((unsigned)(energy*110)<<24);
    float travel=.5f-.5f*cosf(scene_state[scene].page.row*3.14159265359f+
                                  scene_state[scene].page.camera*.08f);
    if(travel<0)travel=0;
    if(travel>1)travel=1;
    /* Thin spatial light trails only: never flash a translucent full-page quad. */
    for(int i=2;i>=0;i--){
        unsigned c=(color&0xffffffu)|((unsigned)(energy*(36-i*10))<<24);
        if(theme==VT_UI_THEME_SIGNAL){
            float edge=x+width-4-i*3;
            vita2d_draw_line(edge,top+12,edge-18,bottom-12,c);
        }else if(theme==VT_UI_THEME_APERTURE||theme==VT_UI_THEME_MOSAIC){
            vita2d_draw_rectangle(x+8+i,top+i,width-16-i*2,1,c);
            vita2d_draw_rectangle(x+8+i,bottom-i-2,width-16-i*2,1,c);
        }else vita2d_draw_rectangle(x+1+i,top+4,1,bottom-top-8,c);
    }
    float y=top+6+travel*fmaxf(0,bottom-top-40);
    if(theme==VT_UI_THEME_RELAY){
        vita2d_draw_rectangle(x+3,y,18,2,color);
        vita2d_draw_rectangle(x+19,y,2,12,color);
    }else if(theme==VT_UI_THEME_DECK_90)
        vita2d_draw_rectangle(x+4,y,16,2,color);
    else if(theme==VT_UI_THEME_GHOST_MESH)
        vita2d_draw_rectangle(x+3,y,2,24,color);
}
void ui_theme_scene_begin(UiThemeScene scene, const char *identity) {
    if (scene < 0 || scene >= UI_SCENE_COUNT || vt_ui_theme_legacy_layout(ui_theme_id()))
        return;
    uint64_t hash = 1469598103934665603ULL;
    if (identity)
        for (const unsigned char *p = (const unsigned char *)identity; *p; p++) {
            hash ^= *p;
            hash *= 1099511628211ULL;
        }
    uint64_t now = sceKernelGetProcessTimeWide();
    if (!scene_state[scene].ready || scene_state[scene].theme != ui_theme_id() ||
        scene_state[scene].hash != hash) {
        scene_state[scene].hash = hash;
        scene_state[scene].changed = now;
        scene_state[scene].theme = ui_theme_id();
        scene_state[scene].ready = 1;
    }
}
void ui_theme_scene_enter(UiThemeScene scene, const char *identity) {
    if (scene < 0 || scene >= UI_SCENE_COUNT || vt_ui_theme_legacy_layout(ui_theme_id()))
        return;
    scene_state[scene].ready = 0;
    ui_theme_scene_begin(scene, identity);
}
static float scene_reveal_amount(UiThemeScene scene, int slot) {
    (void)slot;
    /* Lists and command decks move as one surface. Their container/viewport
     * already animates, so replaying staggered object entrances causes flicker. */
    if (scene==UI_SCENE_HOME || scene==UI_SCENE_BROWSER || scene==UI_SCENE_MENU ||
        vt_preferences_reduce_motion() || !scene_state[scene].ready)
        return 1;
    uint64_t now = sceKernelGetProcessTimeWide(), changed = scene_state[scene].changed;
    float t = now >= changed ? (now - changed) / 1000.0f : 1000;
    float delay = 0;
    float duration = ui_theme_id() == VT_UI_THEME_DECK_90    ? 170
                     : ui_theme_id() == VT_UI_THEME_APERTURE ? 300
                                                             : 240;
    t = (t - delay) / duration;
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    /* Each system uses its own physical gesture, never an overshooting spring. */
    return ui_theme_id() == VT_UI_THEME_SIGNAL ? t * t * (3 - 2 * t)
                                               : 1 - (1 - t) * (1 - t) * (1 - t);
}
UiThemeRect ui_theme_scene_reveal(UiThemeScene scene, UiThemeRect r, int slot) {
    if (vt_ui_theme_legacy_layout(ui_theme_id()))
        return r;
    float k = 1 - scene_reveal_amount(scene, slot);
    int theme = ui_theme_id();
    if (theme == VT_UI_THEME_GHOST_MESH) {
        r.x += (int)(k * 18);
        r.y -= (int)(k * 8);
    } else if (theme == VT_UI_THEME_DECK_90)
        r.x += (int)(k * 46);
    else if (theme == VT_UI_THEME_SIGNAL)
        r.x += (int)(k * (r.x + r.w * .5f < 480 ? -12 : 12));
    else if (theme == VT_UI_THEME_APERTURE) {
        r.y += (int)(k * (r.h<100?6:18));
        r.x += (int)(k * 6);
    } else if (theme == VT_UI_THEME_RELAY) {
        r.x += (int)(k * 22);
        r.y += (int)(k * 6);
    } else if (theme == VT_UI_THEME_MOSAIC) {
        r.x += (int)(k * ((slot%5+5)%5-2)*6);
        r.y += (int)(k * (r.h<100?6:16));
    }
    return r;
}
void ui_theme_scene_model(UiThemeScene scene, float x, float y, float size, float opacity,
                          int running) {
    int theme = ui_theme_id();
    if (vt_ui_theme_legacy_layout(theme) || opacity <= .01f || scene < 0 || scene >= UI_SCENE_COUNT)
        return;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (scene_state[scene].last && now >= scene_state[scene].last &&
        now - scene_state[scene].last < 100000ULL && running)
        scene_state[scene].clock += now - scene_state[scene].last;
    scene_state[scene].last = now;
    float turn = vt_preferences_reduce_motion()
                     ? 0
                     : (scene_state[scene].clock % 24000000ULL) / 24000000.0f * 6.28318530718f;
    scene_count = 0;
    scene_x = x;
    scene_y = y;
    scene_scale = size / 200;
    scene_opacity = opacity;
    unsigned cold = VT_THEME_COLD, hot = VT_THEME_SIGNAL_BRIGHT;
    if (theme == VT_UI_THEME_GHOST_MESH) {
        scene_ring(94, 3, .7f, .3f, turn, cold);
        scene_ring(72, 4, -.4f, .8f, -turn, hot);
        scene_box(0, 0, 0, 42, 42, 42, .5f, turn, .6f, cold);
    } else if (theme == VT_UI_THEME_DECK_90) {
        /* Receding terminal ribs: a continuous conveyor, not a spinning icon. */
        for (int i = 0; i < 8; i++) {
            float z = (i + (turn / 6.28318530718f)) * 64 - 110;
            unsigned c = scene_alpha(hot, .85f - i * .07f);
            scene_box(-98, 0, z, 3, 126, 3, 0, 0, 0, c);
            scene_box(98, 0, z, 3, 126, 3, 0, 0, 0, c);
            scene_box(0, -63, z, 198, 3, 3, 0, 0, 0, c);
            scene_box(0, 63, z, 198, 3, 3, 0, 0, 0, c);
        }
    } else if (theme == VT_UI_THEME_SIGNAL) {
        /* Counter-rotating signal shutters, with visible backs and thickness. */
        for (int i = 0; i < 5; i++)
            scene_box((i - 2) * 42, 0, (i % 2) * 24, 32, 142, 8, .16f,
                      .35f + sinf(turn + (i * .38f)) * .48f, -.18f, i == 2 ? hot : cold);
    } else if (theme == VT_UI_THEME_APERTURE) {
        /* Five real depth gates: the opening travels toward the foreground. */
        float travel=turn/6.28318530718f*72;
        for(int i=4;i>=0;i--){
            float z=-145+i*76+travel;
            unsigned c=scene_alpha(i==0?hot:cold,1-i*.13f);
            scene_box(-124,0,z,5,162,6,0,0,0,c);
            scene_box(124,0,z,5,162,6,0,0,0,c);
            scene_box(0,-81,z,252,5,6,0,0,0,c);
            scene_box(0,81,z,252,5,6,0,0,0,c);
        }
    } else if (theme == VT_UI_THEME_RELAY) {
        /* A spatial routing graph: nodes and solid links, not another rack. */
        static const SceneVec nodes[6]={{-110,-55,-30},{-25,-85,30},{70,-35,-25},
                                        {-75,60,40},{25,35,-40},{110,75,30}};
        for(int i=0;i<6;i++){
            SceneVec n=scene_rotate(nodes[i],-.25f,.35f+sinf(turn)*.12f,0);
            scene_box(n.x,n.y,n.z,18,18,18,.25f,.5f,0,i==2?hot:cold);
            if(i){
                SceneVec a=scene_rotate(nodes[i-1],-.25f,.35f+sinf(turn)*.12f,0);
                float dx=n.x-a.x,dy=n.y-a.y;
                scene_box((n.x+a.x)*.5f,(n.y+a.y)*.5f,(n.z+a.z)*.5f,
                          sqrtf(dx*dx+dy*dy),3,3,0,0,atan2f(dy,dx),cold);
            }
        }
    } else if (theme == VT_UI_THEME_MOSAIC) {
        /* Five hinged file leaves share a spine and open as an accordion. */
        for(int i=4;i>=0;i--){
            float spread=(i-2)*.32f+sinf(turn)*.055f;
            scene_box((i-2)*36,0,abs(i-2)*26,54,164,5,.06f,spread,
                      (i-2)*.045f,i==2?hot:cold);
            scene_box((i-2)*36,-88,abs(i-2)*26,32,12,6,.06f,spread,
                      (i-2)*.045f,i==2?hot:cold);
        }
    }
    scene_flush();
}
void ui_theme_scene_background(void) {
    if (vt_ui_theme_legacy_layout(ui_theme_id()) || ui_theme_id() == VT_UI_THEME_GHOST_MESH)
        return;
    int theme = ui_theme_id();
    /* Peripheral stage keeps type/thumbnail surfaces opaque and the scene visible. */
    float x = theme == VT_UI_THEME_DECK_90    ? 790
              : theme == VT_UI_THEME_APERTURE ? 160
              : theme == VT_UI_THEME_RELAY    ? 210
                                              : 790;
    float y = theme == VT_UI_THEME_APERTURE ? 282 : theme == VT_UI_THEME_RELAY ? 310 : 390;
    if(theme==VT_UI_THEME_APERTURE){x=480;y=266;}
    if(theme==VT_UI_THEME_MOSAIC){x=480;y=330;}
    ui_theme_scene_model(UI_SCENE_HOME, x, y, theme==VT_UI_THEME_APERTURE?830:theme==VT_UI_THEME_RELAY?320:330,
                         theme==VT_UI_THEME_APERTURE?.22f:.34f, 1);
}
static void scene_card_motion(UiThemeScene role, UiThemeRect r, int selected, int slot) {
    int theme = ui_theme_id();
    if (vt_ui_theme_legacy_layout(theme))
        return;
    /* A shallow extruded object, fitted strictly inside its interaction rectangle. */
    scene_count = 0;
    scene_x = r.x + r.w * .5f;
    scene_y = r.y + r.h * .5f;
    scene_scale = 1;
    scene_opacity = selected ? .9f : .48f;
    float tilt = theme == VT_UI_THEME_RELAY    ? -.22f
                 : theme == VT_UI_THEME_MOSAIC ? (slot % 2 ? .1f : -.1f)
                                               : -.07f;
    float yaw = theme == VT_UI_THEME_SIGNAL ? -.12f : theme == VT_UI_THEME_DECK_90 ? .04f : .08f;
    float k = 1 - scene_reveal_amount(role, slot);
    float camera=vt_preferences_reduce_motion()?0:scene_state[role].page.camera;
    float z = 28;
    /* The same continuous camera drives depth and its labels/hit rectangle.
     * Faces never disappear or replay a per-item entrance when data changes. */
    if(theme==VT_UI_THEME_GHOST_MESH)yaw+=camera*.015f;
    else if(theme==VT_UI_THEME_SIGNAL)yaw-=camera*.025f;
    else if(theme==VT_UI_THEME_APERTURE)z+=fabsf(camera)*8;
    else if(theme==VT_UI_THEME_RELAY)tilt+=camera*.008f;
    else if(theme==VT_UI_THEME_MOSAIC)yaw+=camera*.02f;
    if (theme == VT_UI_THEME_DECK_90)
        z += k * 160;
    else if (theme == VT_UI_THEME_SIGNAL)
        yaw -= k * 1.1f;
    else if (theme == VT_UI_THEME_APERTURE) {
        z += k * 420;tilt=0;yaw=0;
    } else if (theme == VT_UI_THEME_RELAY) {
        tilt=-.08f;z+=k*100;
    } else if (theme == VT_UI_THEME_MOSAIC) {
        tilt=0;yaw=((slot%5+5)%5-2)*(.08f+k*.28f);z+=abs((slot%5+5)%5-2)*7;
    }
    scene_box(0, 0, z, r.w - 16, r.h - 14, theme == VT_UI_THEME_RELAY ? 18 : 8, tilt, yaw, 0,
              selected ? VT_THEME_SIGNAL_BRIGHT : VT_THEME_COLD);
    scene_flush();
}
UiThemeHomeLayout ui_theme_browser_layout(int theme, int bottom, int grid) {
    UiThemeHomeLayout l = {18, 110, 924, 54, 6, 1};
    if (theme == VT_UI_THEME_GHOST_MESH)
        l = (UiThemeHomeLayout){32, 124, 438, 96, 6, 2};
    else if (theme == VT_UI_THEME_DECK_90)
        l = (UiThemeHomeLayout){72, 124, 816, 51, 7, 1};
    else if (theme == VT_UI_THEME_SIGNAL)
        l = (UiThemeHomeLayout){32, 130, 436, 146, 4, 2};
    else if (theme == VT_UI_THEME_APERTURE)
        l = (UiThemeHomeLayout){52, 142, 856, bottom-174>300?300:bottom-174, 1, 1};
    else if (theme == VT_UI_THEME_RELAY)
        l = (UiThemeHomeLayout){52, 134, 380, 134, 4, 2};
    else if (theme == VT_UI_THEME_MOSAIC)
        l = (UiThemeHomeLayout){30, 140, 164, bottom-210>298?298:bottom-210, 5, 5};
    if (!grid && !vt_ui_theme_legacy_layout(theme)) {
        l.w = theme == VT_UI_THEME_DECK_90 ? 816 : 928 - l.x;
        l.columns = 1;
        l.step = 54;
        l.rows = 7;
    }
    int limit = (bottom - l.y + 4) / l.step * l.columns;
    if (limit < l.columns)
        limit = l.columns;
    if (l.rows > limit)
        l.rows = limit;
    return l;
}
UiThemeRect ui_theme_browser_rect(int theme, int slot, int bottom, int grid) {
    UiThemeHomeLayout l = ui_theme_browser_layout(theme, bottom, grid);
    int row=slot/l.columns,col=slot%l.columns;if(col<0){col+=l.columns;row--;}
    UiThemeRect r = {l.x + col * (l.w + 12), l.y + row * l.step, l.w,
                     l.step - 12};
    if (theme == VT_UI_THEME_RELAY && grid){
        r.x=52+col*446;r.y=134+row*134+col*26;r.h=100;
    }else if(theme==VT_UI_THEME_MOSAIC && grid){
        r.x+=row*960;r.y=140+abs(col-2)*18;
        r.h=bottom-34-176;if(r.h>286)r.h=286;
    }
    return ui_theme_page_rect(UI_SCENE_BROWSER, r);
}
UiAudioLayout ui_theme_audio_layout(int layout, int has_cover) {
    UiAudioLayout l = ui_audio_layout(layout, has_cover);
    int theme = ui_theme_id();
    if (vt_ui_theme_legacy_layout(theme))
        return l;
    if (layout == VT_AUDIO_LAYOUT_CENTER) {
        if (theme == VT_UI_THEME_DECK_90)
            l = (UiAudioLayout){124, 122, 242, 418, 480, 174};
        else if (theme == VT_UI_THEME_SIGNAL)
            l = (UiAudioLayout){42, 102, 288, 388, 520, 172};
        else if (theme == VT_UI_THEME_RELAY)
            l = (UiAudioLayout){558, 104, 268, 48, 454, 174};
        else if (theme == VT_UI_THEME_MOSAIC)
            l = (UiAudioLayout){110, 96, 292, 466, 444, 170};
        else if (theme == VT_UI_THEME_GHOST_MESH)
            l = (UiAudioLayout){364, 82, 232, 200, 560, 338};
        else if (theme == VT_UI_THEME_APERTURE)
            l = (UiAudioLayout){370, 90, 220, 190, 580, 338};
    } else {
        l.cover_size = theme == VT_UI_THEME_APERTURE ? 264 : theme == VT_UI_THEME_RELAY ? 286 : 300;
        l.cover_y = 100;
        l.cover_x = layout == VT_AUDIO_LAYOUT_LEFT ? 42 : 918 - l.cover_size;
        l.title_y = theme == VT_UI_THEME_SIGNAL ? 164 : 184;
    }
    /* Reserve the same stage when art is absent: the 3D object occupies it. */
    return l;
}
void ui_theme_scene_transport(float x, float y, float w, float h, int selected, float opacity) {
    int theme = ui_theme_id();
    if (vt_ui_theme_legacy_layout(theme) || opacity <= .01f)
        return;
    unsigned hot = scene_alpha(VT_THEME_SIGNAL_BRIGHT, opacity),
             cold = scene_alpha(VT_THEME_COLD, opacity * .65f);
    if (theme == VT_UI_THEME_GHOST_MESH || theme == VT_UI_THEME_APERTURE) {
        int n = theme == VT_UI_THEME_APERTURE ? 48 : 6;
        float radius = fminf(w, h) * .5f + 3;
        for (int i = 0; i < n; i++) {
            float a = i * 6.28318530718f / n, b = (i + 1) * 6.28318530718f / n;
            vita2d_draw_line(x + w * .5f + cosf(a) * radius, y + h * .5f + sinf(a) * radius,
                             x + w * .5f + cosf(b) * radius, y + h * .5f + sinf(b) * radius,
                             selected ? hot : cold);
        }
    } else if (theme == VT_UI_THEME_DECK_90) {
        vita2d_draw_rectangle(x - 4, y - 3, 3, h + 6, hot);
        vita2d_draw_rectangle(x + w + 1, y - 3, 3, h + 6, hot);
    } else if (theme == VT_UI_THEME_SIGNAL) {
        for (int i = 0; i < 6; i++)
            vita2d_draw_line(x + w + i, y, x + w - 8 + i, y + h, cold);
        vita2d_draw_rectangle(x, y + h, w, 3, hot);
    } else if (theme == VT_UI_THEME_RELAY) {
        vita2d_draw_line(x - 6, y + 4, x - 6, y + h + 6, cold);
        vita2d_draw_line(x - 6, y + h + 6, x + w - 6, y + h + 6, hot);
        vita2d_draw_line(x + w - 6, y + h + 6, x + w, y + h, cold);
    } else if (theme == VT_UI_THEME_MOSAIC) {
        vita2d_draw_rectangle(x - 4, y - 4, 9, 2, hot);
        vita2d_draw_rectangle(x - 4, y - 4, 2, 9, hot);
        vita2d_draw_rectangle(x + w - 5, y + h + 2, 9, 2, cold);
    }
}

static void scene_artwork(float x,float y,float size,vita2d_texture *art,int folder){
 ui_artwork_tile(x,y,size,art,folder?NULL:"...");
 if(!art&&folder){
  vita2d_draw_rectangle(x+size*.1f,y+size*.22f,size*.36f,size*.13f,VT_THEME_COLD);
  vita2d_draw_rectangle(x+size*.1f,y+size*.33f,size*.8f,size*.5f,VT_THEME_BLUE);
  vita2d_draw_rectangle(x+size*.1f,y+size*.33f,size*.8f,2,VT_THEME_COLD);
 }
}
void ui_theme_browser_tile(UiThemeScene role, UiThemeRect r, int slot, int selected,
                           vita2d_texture *art, const char *title, const char *detail, int folder) {
    int theme = ui_theme_id();
    scene_card_motion(role, r, selected, slot);
    if(theme==VT_UI_THEME_APERTURE && r.h>120){
        unsigned fill=selected?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE;
        vita2d_draw_rectangle(r.x+8,r.y+8,r.w-16,r.h-16,fill);
        int size=r.h-32;
        if(art)scene_picture(art,r.x+18,r.y+16,size,0,-.11f,0);
        else scene_artwork(r.x+18,r.y+16,size,NULL,folder);
        int tx=r.x+size+48,width=r.w-size-74;
        ui_text_line(tx,r.y+r.h*.43f,width,title,VT_THEME_TEXT,UI_FONT_DISPLAY);
        ui_text_line(tx,r.y+r.h*.43f+34,width,detail,VT_THEME_TEXT_MUTED,UI_FONT_BODY);
        vita2d_draw_rectangle(tx,r.y+24,width,2,selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_COLD);
        return;
    }
    if(theme==VT_UI_THEME_RELAY && r.h>=90){
        vita2d_draw_line(r.x-10,r.y+r.h*.5f,r.x+r.w+10,r.y+r.h*.5f,VT_THEME_COLD);
        vita2d_draw_rectangle(r.x+8,r.y+8,r.w-16,r.h-16,selected?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE);
        int size=r.h-28;scene_artwork(r.x+14,r.y+14,size,art,folder);
        ui_text_line(r.x+size+28,r.y+42,r.w-size-44,title,selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT,UI_FONT_SMALL);
        ui_text_line(r.x+size+28,r.y+66,r.w-size-44,detail,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        vita2d_draw_rectangle(r.x+r.w-8,r.y+r.h*.5f-5,10,10,selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_COLD);
        return;
    }
    if(theme==VT_UI_THEME_MOSAIC && r.h>110){
        vita2d_draw_rectangle(r.x+6,r.y+8,r.w-12,r.h-14,selected?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE);
        vita2d_draw_rectangle(r.x+8,r.y+2,r.w*.56f,10,selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_COLD);
        int size=r.w-32;if(size>r.h-78)size=r.h-78;
        int ax=r.x+(r.w-size)/2,ay=r.y+20;
        if(art)scene_picture(art,ax,ay,size,.02f,((slot%5+5)%5-2)*.16f,((slot%5+5)%5-2)*.015f);
        else scene_artwork(ax,ay,size,NULL,folder);
        ui_text_line(r.x+14,r.y+r.h-43,r.w-28,title,selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT,UI_FONT_SMALL);
        ui_text_line(r.x+14,r.y+r.h-20,r.w-28,detail,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        return;
    }
    /* Keep labels on the front plane; the bevel and hinge belong to the 3D shell. */
    unsigned fill = selected ? VT_THEME_SURFACE_RAISED : VT_THEME_SURFACE;
    vita2d_draw_rectangle(r.x + 7, r.y + 7, r.w - 14, r.h - 14, fill);
    int horizontal = r.h < 100 || theme == VT_UI_THEME_DECK_90;
    int size = horizontal ? r.h - 22 : r.h - 76;
    if (size > r.w - 36)
        size = r.w - 36;
    if (size < 20)
        size = 20;
    int ax = r.x + 14, ay = r.y + 12;
    if (!horizontal && (theme == VT_UI_THEME_APERTURE || theme == VT_UI_THEME_MOSAIC))
        ax = r.x + (r.w - size) / 2;
    ui_artwork_tile(ax, ay, size, art, folder ? "DIR" : "...");
    int tx = horizontal ? ax + size + 14 : r.x + 18,
        ty = horizontal ? r.y + (r.h < 50 ? 17 : 24) : r.y + r.h - 43;
    int width = horizontal ? r.w - size - 42 : r.w - 36;
    ui_text_line(tx, ty, width, title, selected ? VT_THEME_SIGNAL_BRIGHT : VT_THEME_TEXT,
                 UI_FONT_SMALL);
    ui_text_line(tx, ty + (r.h < 50 ? 17 : 22), width, detail, VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
    ui_theme_scene_transport(r.x + 8, r.y + 8, r.w - 16, r.h - 16, selected, selected ? .8f : .3f);
}

UiThemeVideoLayout ui_theme_video_layout(void) {
    switch (ui_theme_id()) {
    case VT_UI_THEME_GHOST_MESH:
        return (UiThemeVideoLayout){400, 480, 560, 458, 48, 509, 864};
    case VT_UI_THEME_DECK_90:
        return (UiThemeVideoLayout){352, 428, 504, 452, 78, 509, 804};
    case VT_UI_THEME_SIGNAL:
        return (UiThemeVideoLayout){344, 422, 500, 460, 32, 514, 896};
    case VT_UI_THEME_APERTURE:
        return (UiThemeVideoLayout){394, 480, 566, 452, 96, 509, 768};
    case VT_UI_THEME_RELAY:
        return (UiThemeVideoLayout){346, 422, 498, 458, 64, 514, 832};
    case VT_UI_THEME_MOSAIC:
        return (UiThemeVideoLayout){368, 444, 520, 450, 28, 509, 904};
    default:
        return (UiThemeVideoLayout){400, 480, 560, 458, 48, 509, 864};
    }
}
static void scene_picture(vita2d_texture *art, float x, float y, float size, float pitch, float yaw, float roll) {
    if (!art)
        return;
    if (vt_ui_theme_legacy_layout(ui_theme_id()))
        return;
    SceneVec plane[4] = {{-size * .5f, -size * .5f, 0},
                         {size * .5f, -size * .5f, 0},
                         {size * .5f, size * .5f, 0},
                         {-size * .5f, size * .5f, 0}};
    vita2d_texture_vertex vertices[6];
    static const int index[6] = {0, 1, 2, 0, 2, 3};
    float tw = vita2d_texture_get_width(art), th = vita2d_texture_get_height(art);
    if (tw < 1 || th < 1)
        return;
    /* Central square crop matches the tile frames, never stretches rectangular art. */
    float extent = fminf(tw, th), u0 = (tw - extent) * .5f / tw, v0 = (th - extent) * .5f / th;
    float u1 = u0 + extent / tw, v1 = v0 + extent / th;
    for (int i = 0; i < 6; i++) {
        int j = index[i];
        SceneVec p = scene_rotate(plane[j], pitch, yaw, roll);
        float scale = 620 / (620 + p.z);
        vertices[i] =
            (vita2d_texture_vertex){x + size * .5f + p.x * scale, y + size * .5f + p.y * scale, .5f,
                                    j == 0 || j == 3 ? u0 : u1, j < 2 ? v0 : v1};
    }
#ifdef __vita__
    vita2d_texture_vertex *gpu = vita2d_pool_memalign(sizeof(vertices), 16);
    if (gpu) {
        memcpy(gpu, vertices, sizeof(vertices));
        vita2d_draw_array_textured(art, SCE_GXM_PRIMITIVE_TRIANGLES, gpu, 6, 0xffffffff);
    }
#else
    vita2d_draw_array_textured(art, SCE_GXM_PRIMITIVE_TRIANGLES, vertices, 6, 0xffffffff);
#endif
}

void ui_theme_scene_art(vita2d_texture *art,float x,float y,float size,int running){
    uint64_t ticks = scene_state[UI_SCENE_AUDIO].clock;
    float t =
        vt_preferences_reduce_motion() ? 0 : (ticks % 24000000ULL) / 24000000.0f * 6.28318530718f;
    int theme = ui_theme_id();
    float yaw = theme == VT_UI_THEME_SIGNAL   ? -.22f
                : theme == VT_UI_THEME_RELAY  ? .21f
                : theme == VT_UI_THEME_MOSAIC ? -.15f
                                              : 0;
    float pitch = theme == VT_UI_THEME_DECK_90 ? -.15f : theme == VT_UI_THEME_RELAY ? -.18f : .04f;
    if (!vt_preferences_reduce_motion())
        yaw += sinf(t) * .035f;
    if(theme==VT_UI_THEME_APERTURE){yaw=.18f;pitch=0;}
    if(theme==VT_UI_THEME_RELAY){yaw=-.24f;pitch=-.1f;}
    if(theme==VT_UI_THEME_MOSAIC){yaw=.28f;pitch=.03f;}
    scene_picture(art,x,y,size,pitch,yaw,0);
    (void)running;
}

void ui_theme_scene_card(UiThemeScene role, UiThemeRect r, int selected, int slot) {
    scene_card_motion(role, r, selected, slot);
}
void ui_theme_video_scrim(float opacity) {
    unsigned base = scene_alpha(RGBA8(0, 0, 0, 214), opacity);
    int theme = ui_theme_id();
    if (theme == VT_UI_THEME_GHOST_MESH) {
        vita2d_draw_rectangle(32, 426, 154, 42, base);
        vita2d_draw_rectangle(340, 426, 284, 52, base);
        vita2d_draw_rectangle(600, 426, 344, 42, base);
        vita2d_draw_rectangle(32, 480, 896, 52, base);
    } else if (theme == VT_UI_THEME_DECK_90) {
        vita2d_draw_rectangle(64, 420, 832, 112, base);
        for (int y = 422; y < 532; y += 8)
            vita2d_draw_rectangle(66, y, 828, 1, scene_alpha(VT_THEME_COLD, opacity * .12f));
        vita2d_draw_rectangle(64, 420, 832, 2, scene_alpha(VT_THEME_SIGNAL_BRIGHT, opacity));
    } else if (theme == VT_UI_THEME_SIGNAL) {
        vita2d_draw_rectangle(24, 420, 912, 112, base);
        for (int i = 0; i < 12; i++)
            vita2d_draw_line(24 + i, 420, 24, 432 + i,
                             scene_alpha(VT_THEME_SIGNAL_BRIGHT, opacity * .9f));
        vita2d_draw_rectangle(24, 530, 912, 3, scene_alpha(VT_THEME_SIGNAL_BRIGHT, opacity));
    } else if (theme == VT_UI_THEME_APERTURE) {
        vita2d_draw_rectangle(32, 420, 156, 48, base);
        vita2d_draw_rectangle(340, 420, 280, 57, base);
        vita2d_draw_rectangle(604, 420, 340, 48, base);
        vita2d_draw_rectangle(80, 480, 800, 52, base);
        for (int x = 96; x < 872; x += 24)
            vita2d_draw_rectangle(x, 529, 10, 3, scene_alpha(VT_THEME_COLD, opacity * .7f));
    } else if (theme == VT_UI_THEME_RELAY) {
        vita2d_draw_rectangle(32, 422, 156, 48, base);
        vita2d_draw_rectangle(310, 420, 226, 62, base);
        vita2d_draw_rectangle(600, 422, 344, 48, base);
        vita2d_draw_rectangle(48, 482, 864, 54, base);
        vita2d_draw_line(48, 536, 912, 536, scene_alpha(VT_THEME_SIGNAL_BRIGHT, opacity * .9f));
    } else if (theme == VT_UI_THEME_MOSAIC) {
        vita2d_draw_rectangle(32, 420, 156, 48, base);
        vita2d_draw_rectangle(328, 416, 228, 64, base);
        vita2d_draw_rectangle(600, 420, 344, 48, base);
        vita2d_draw_rectangle(16, 484, 928, 48, base);
        vita2d_draw_rectangle(328, 416, 32, 3, scene_alpha(VT_THEME_COLD, opacity));
    }
}
