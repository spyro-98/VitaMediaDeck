/* Actual dashboard entry contract; native trust/auth/browser are controlled
 * adapters. No real account or trust prompt is claimed by this host fixture. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/network_sources_screen.h"
#include "network/network_internal.h"
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        assert(x);                                                                                 \
    } while (0)
static struct {
    int valid;
    VtNetworkSource source;
} g_browser_resume;
static int ssh_result, tls_result, persist_result, auth_result, browser_results[2];
static int auth_calls, browser_calls, persist_calls, tls_calls, restored[2];
static int prepare_sftp_trust(VtNetworkSource *s) {
    if (s->protocol != VT_NETWORK_SFTP)
        return 0;
    if (ssh_result > 0)
        strcpy(s->host_key_sha256, "fixture-host-pin");
    return ssh_result;
}
static int prepare_https_trust(VtNetworkSource *s, const VtNetworkCredential *c) {
    CHECK(c->password[0]);
    tls_calls++;
    if (tls_result > 0)
        strcpy(s->tls_public_key_sha256, "fixture-tls-pin");
    return tls_result;
}
static int library_persist_source(const VtNetworkSource *s) {
    CHECK(s->host_key_sha256[0] || s->tls_public_key_sha256[0]);
    persist_calls++;
    return persist_result;
}
int ui_network_library_authorize(VtNetworkSource *s, VtNetworkCredential *c) {
    CHECK(s && c && c->password[0]);
    auth_calls++;
    return auth_result;
}
static int network_source_matches(const VtNetworkSource *a, const VtNetworkSource *b) {
    return !strcmp(a->host, b->host);
}
static int browse_source(const VtNetworkSource *s, const VtNetworkCredential *c,
                         UiNetworkSelection *out, int restore) {
    CHECK(s && c && out && c->password[0]);
    CHECK(browser_calls < 2);
    restored[browser_calls] = restore;
    return browser_results[browser_calls++];
}
#include "connections.inc"
static void reset(void) {
    ssh_result = tls_result = persist_result = auth_result = 0;
    auth_calls = browser_calls = persist_calls = tls_calls = 0;
    memset(restored, 0, sizeof(restored));
    browser_results[0] = browser_results[1] = UI_NETWORK_ACTION_PLAY;
    g_browser_resume.valid = 1;
    strcpy(g_browser_resume.source.host, "fixture.server");
}
int main(void) {
    VtNetworkSource source = {.protocol = VT_NETWORK_SFTP};
    strcpy(source.host, "fixture.server");
    VtNetworkCredential credential = {0};
    strcpy(credential.password, "fixture-secret");
    UiNetworkSelection selection = {0};
    reset();
    CHECK(ui_network_browse_saved(NULL, &credential, &selection) == UI_NETWORK_ACTION_BACK);
    CHECK(!auth_calls && !browser_calls);
    reset();
    ssh_result = -1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) < 0);
    CHECK(!auth_calls && !browser_calls && !persist_calls);
    reset();
    ssh_result = 1;
    persist_result = -1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) < 0);
    CHECK(persist_calls == 1 && !auth_calls && !browser_calls);
    reset();
    ssh_result = 1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) == UI_NETWORK_ACTION_PLAY);
    CHECK(persist_calls == 1 && auth_calls == 1 && browser_calls == 1 && restored[0]);
    reset();
    source.protocol = VT_NETWORK_JELLYFIN;
    auth_result = -1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) < 0);
    CHECK(auth_calls == 1 && !browser_calls);
    reset();
    source.protocol = VT_NETWORK_WEBDAV;
    browser_results[0] = VT_NETWORK_TLS_TRUST_REQUIRED;
    tls_result = -1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) ==
          VT_NETWORK_TLS_TRUST_REQUIRED);
    CHECK(tls_calls == 1 && browser_calls == 1 && !persist_calls);
    reset();
    browser_results[0] = VT_NETWORK_TLS_TRUST_REQUIRED;
    tls_result = 1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) == UI_NETWORK_ACTION_PLAY);
    CHECK(persist_calls == 1 && browser_calls == 2 && restored[0] && !restored[1]);
    reset();
    browser_results[0] = VT_NETWORK_TLS_TRUST_REQUIRED;
    tls_result = 1;
    persist_result = -1;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) < 0);
    CHECK(browser_calls == 1 && persist_calls == 1);
    reset();
    source.protocol = VT_NETWORK_NAVIDROME;
    browser_results[0] = UI_NETWORK_ACTION_SECTION_BASE + 4;
    CHECK(ui_network_browse_saved(&source, &credential, &selection) == browser_results[0]);
    CHECK(auth_calls == 1 && browser_calls == 1 && !tls_calls);
    printf("Saved connection entry: %u checks; declined/accepted trust, persistence failure, auth "
           "failure, direct browser, restored navigation and section propagation passed.\n",
           checks);
    return 0;
}
