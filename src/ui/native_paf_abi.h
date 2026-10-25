/* Minimal, app-owned C declarations for public reverse-engineered PAF ABI.
 * No Sony implementation, SDK header or firmware asset is incorporated.
 * The exact symbols/NIDs and reference revision are recorded in release/paf-imports.json.
 */
#ifndef VMD_NATIVE_PAF_ABI_H
#define VMD_NATIVE_PAF_ABI_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const void *data;
    uint32_t count, allocator_padding;
} VtPafString;
typedef struct {
    VtPafString name;
    uint32_t hash;
} VtPafId;
#if defined(__vita__)
_Static_assert(sizeof(VtPafString) == 12 && sizeof(VtPafId) == 16, "PAF ARM ABI");
#endif
void vmd_paf_fw_params(void *params);
void vmd_paf_fw_create(void *self, const void *params);
void *vmd_paf_fw_instance(void);
void vmd_paf_fw_update(void *self, void *params);
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
int vmd_paf_widget_show(void *self, void *timer, int options, float delay, void *finished);
int vmd_paf_widget_hide(void *self, void *timer, int options, float delay, void *finished);
int vmd_paf_widget_alpha(void *self, float alpha, void *timer, int id, int options, float delay,
                         void *finished);
int vmd_paf_button_text(void *self, const VtPafString *text, int object);
uint32_t vmd_paf_button_state(void *self, int state);
int vmd_paf_widget_text(void *self, const VtPafString *text, int object);

int vmd_paf_widget_color(void *self, const float color[4], void *timer, int id, int options,
                         float delay, void *finished, void *data);
int vmd_paf_widget_order(void *self, void *child, void *sibling);
int vmd_paf_text_size(void *self, int attr, unsigned start, unsigned count, float value);
int vmd_paf_text_color(void *self, int attr, unsigned start, unsigned count, const float value[4]);
int vmd_paf_text_layout_int(void *self, int attr, int value);
int vmd_paf_text_layout_bool(void *self, int attr, int value);
int vmd_paf_text_layout_vec(void *self, int attr, const float value[2]);
void *vmd_paf_widget_find(void *self, const VtPafId *name, int options);
void vmd_paf_busy_start(void *self);
void vmd_paf_busy_stop(void *self);
#endif
