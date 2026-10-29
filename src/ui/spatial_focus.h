#ifndef VMD_SPATIAL_FOCUS_H
#define VMD_SPATIAL_FOCUS_H

/* Cardinal navigation follows the same rectangles as drawing and touch.
 * Disabled/hidden nodes never enter the graph; no directional wrap or input
 * action occurs here. IDs remain stable while a viewport scrolls. */
typedef struct {float x,y,w,h;int enabled,id;} UiFocusNode;
enum {UI_FOCUS_LEFT,UI_FOCUS_RIGHT,UI_FOCUS_UP,UI_FOCUS_DOWN};
static inline float ui_focus_abs(float x){return x<0?-x:x;}
static inline float ui_focus_max(float a,float b){return a>b?a:b;}
static inline float ui_focus_min(float a,float b){return a<b?a:b;}
static inline int ui_spatial_focus(const UiFocusNode *nodes,int count,int current,int direction){
    int at=-1,first=-1;
    for(int i=0;i<count;i++)if(nodes[i].enabled&&nodes[i].w>0&&nodes[i].h>0){
        if(first<0)first=i;
        if(nodes[i].id==current)at=i;
    }
    if(at<0)return first>=0?nodes[first].id:current;
    const UiFocusNode *a=&nodes[at];
    float best=1e30f;int result=current;
    for(int i=0;i<count;i++){
        const UiFocusNode*b=&nodes[i];
        if(i==at||!b->enabled||b->w<=0||b->h<=0)continue;
        float gap,orthogonal,centers;
        if(direction==UI_FOCUS_LEFT||direction==UI_FOCUS_RIGHT){
            gap=direction==UI_FOCUS_LEFT?a->x-(b->x+b->w):b->x-(a->x+a->w);
            orthogonal=ui_focus_max(0,ui_focus_max(a->y,b->y)-ui_focus_min(a->y+a->h,b->y+b->h));
            centers=ui_focus_abs(a->y+a->h*.5f-b->y-b->h*.5f);
        }else{
            gap=direction==UI_FOCUS_UP?a->y-(b->y+b->h):b->y-(a->y+a->h);
            orthogonal=ui_focus_max(0,ui_focus_max(a->x,b->x)-ui_focus_min(a->x+a->w,b->x+b->w));
            centers=ui_focus_abs(a->x+a->w*.5f-b->x-b->w*.5f);
        }
        if(gap<-.5f)continue;
        float score=ui_focus_max(0,gap)*4+orthogonal*6+centers*.1f;
        if(score<best){best=score;result=b->id;}
    }
    return result;
}
#endif
