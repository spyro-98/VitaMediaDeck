/* Minimal, app-owned C declarations for public reverse-engineered PAF ABI.
 * No Sony implementation, SDK header or firmware asset is incorporated.
 * The exact symbols/NIDs and reference revision are recorded in release/paf-imports.json.
 */
#ifndef VMD_NATIVE_PAF_ABI_H
#define VMD_NATIVE_PAF_ABI_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    const void *data;
    uint32_t count, allocator_padding;
} VtPafString;
/* Retail PAF uses a SIMD-sized Vector2 reference, not an 8-byte float pair.
 * Layout IDs below were checked against the loaded firmware. The pinned RE
 * header omits an integer attribute and shifts every later ID down by one. */
typedef struct {
    _Alignas(16) float lane[4];
} VtPafVec2;
_Static_assert(sizeof(VtPafVec2) == 16 && _Alignof(VtPafVec2) == 16, "PAF Vector2 ABI");
enum {
    VMD_PAF_LAYOUT_ALIGN_X = 0,
    VMD_PAF_LAYOUT_ALIGN_Y = 1,
    VMD_PAF_LAYOUT_TEXT_ALIGN = 2,
    VMD_PAF_LAYOUT_SIZE = 6,
    VMD_PAF_LAYOUT_VISIBLE_POS = 7,
    VMD_PAF_LAYOUT_VISIBLE_SIZE = 8,
    VMD_PAF_LAYOUT_CONDENSE_X = 11,
    VMD_PAF_LAYOUT_WORD_WRAP = 13,
    VMD_PAF_LAYOUT_ELLIPSIS = 15
};
/* The draw-object index is specific to each widget class. Button's index 0
 * is its background plane; only index 1 is a text object (OBJ_LABEL). */
enum { VMD_PAF_TEXT_OBJECT = 0, VMD_PAF_BUTTON_LABEL_OBJECT = 1 };
/* Framework::Update in CommonDialog mode consumes paf::cdlg::OnUpdateData.
 * It is NOT SceCommonDialogUpdateParam: field order and total size differ. */
typedef struct {
    uint32_t pitch_pixels, width, height, color_format, surface_type;
    void *color_data, *depth_data, *display_sync;
} VtPafFrameUpdate;
typedef struct {
    VtPafString name;
    uint32_t hash;
} VtPafId;
#if defined(__vita__)
_Static_assert(sizeof(VtPafString) == 12 && sizeof(VtPafId) == 16, "PAF ARM ABI");
_Static_assert(sizeof(VtPafFrameUpdate) == 32 &&
                   offsetof(VtPafFrameUpdate, pitch_pixels) == 0 &&
                   offsetof(VtPafFrameUpdate, width) == 4 &&
                   offsetof(VtPafFrameUpdate, height) == 8 &&
                   offsetof(VtPafFrameUpdate, color_format) == 12 &&
                   offsetof(VtPafFrameUpdate, surface_type) == 16 &&
                   offsetof(VtPafFrameUpdate, color_data) == 20 &&
                   offsetof(VtPafFrameUpdate, depth_data) == 24 &&
                   offsetof(VtPafFrameUpdate, display_sync) == 28,
               "PAF OnUpdateData ARM ABI");
#endif
void vmd_paf_fw_params(void *params);
void vmd_paf_fw_create(void *self, const void *params);
void *vmd_paf_fw_instance(void);
/* Run is blocking, including with OneFrameMode: the thread that initialized PAF sleeps
 * between Update handshakes. The first Framework word is its run-loop count
 * in the pinned public ABI; only this read-only startup prefix is observed. */
typedef struct { uint32_t run_loop_count; } VtPafFrameworkPrefix;
_Static_assert(sizeof(VtPafFrameworkPrefix) == 4 &&
               offsetof(VtPafFrameworkPrefix, run_loop_count) == 0, "PAF Framework prefix ABI");
int vmd_paf_fw_run(void *self);
void vmd_paf_fw_update(void *self, const VtPafFrameUpdate *params);
void vmd_paf_main_task_register(void (*callback)(void *), void *userdata);
void vmd_paf_fw_common(void *self, void (*finished)(void));
void vmd_paf_plugin_params(void *params);
void vmd_paf_plugin_load(const void *params, void (*finished)(void *), int options);
void vmd_paf_page_params(void *params);
void *vmd_paf_page_open(void *self, const VtPafId *id, const void *params);
void *vmd_paf_widget_create(void *self, void *parent, const char *type, const VtPafId *name,
                            const VtPafId *style);
uint32_t vmd_paf_id_hash(VtPafId *self, const VtPafString *name);
void *vmd_paf_allocate(size_t bytes);
int vmd_paf_widget_pos(void *self, const float v[4], void *timer, int id, int options, float delay,
                       void *finished, void *data);
int vmd_paf_widget_size(void *self, const float v[4], void *timer, int id, int options, float delay,
                        void *finished, void *data);
/* The imported argument ABI matches the public RE declarations. On verified
 * retail code Show/Hide do not return a status: ignore the value in r0. */
int vmd_paf_widget_show(void *self, void *timer, int options, float delay, void *finished);
int vmd_paf_widget_hide(void *self, void *timer, int options, float delay, void *finished);
int vmd_paf_widget_alpha(void *self, float alpha, void *timer, int id, int options, float delay,
                         void *finished);
int vmd_paf_button_text(void *self, const VtPafString *text, int object);
uint32_t vmd_paf_button_state(void *self, int state);
int vmd_paf_widget_text(void *self, const VtPafString *text, int object);

int vmd_paf_widget_color(void *self, const float color[4], void *timer, int id, int options,
                         float delay, void *finished, void *data);
bool vmd_paf_widget_order(void *self, void *child, void *sibling);
int vmd_paf_text_size(void *self, int attr, unsigned start, unsigned count, float value);
int vmd_paf_text_color(void *self, int attr, unsigned start, unsigned count, const float value[4]);
int vmd_paf_text_layout_int(void *self, int attr, int value);
int vmd_paf_text_layout_bool(void *self, int attr, bool value);
int vmd_paf_text_layout_vec(void *self, int attr, const VtPafVec2 *value);
void *vmd_paf_widget_find(void *self, const VtPafId *name, int options);
void vmd_paf_busy_start(void *self);
void vmd_paf_busy_stop(void *self);
/* StateButtonBase::SetCheck(bool,float,bool), owner-thread only; void result. */
void vmd_paf_check_set(void *self, bool checked, float time, bool animate);
/* Typed native Button label objects, never cast a Button to ui::Text. */
void *vmd_paf_widget_draw_obj(void *self, int object);
void vmd_paf_button_animator(void *self, int style);
int vmd_paf_label_layout_vec(void *self, int attr, const VtPafVec2 *value);
int vmd_paf_label_layout_bool(void *self, int attr, bool value);
#endif
