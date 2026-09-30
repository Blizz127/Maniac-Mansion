/* DEV_MENU_SPEC §4a tests: the ignore-list filter, the pad selection
 * policy, and an SDL virtual-device case under the Steam hint environment
 * (a physical Deck/InputPlumber pad hidden by the inherited ignore list
 * plus a silent Steam virtual pad). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/host/pads.h"

static int failures;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);  \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static char last_log[512];
static void cap(const char *s)
{
    snprintf(last_log, sizeof last_log, "%s", s);
    printf("  log: %s\n", s);
}

static void test_filter(void)
{
    char *f = pads_filter_ignore_list("0x28de/0x1205,0x28de/0x11ff,0x045e/0x028e,0x28de/0x12fb,0x17ef/0x6182,"
                                      "0x17ef/*,0x28de/0x1206,0x28de/0x1142");
    CHECK(!strcmp(f, "0x28de/0x11ff,0x045e/0x028e,0x28de/0x1142"));
    free(f);
    f = pads_filter_ignore_list("");
    CHECK(!strcmp(f, ""));
    free(f);
}

static void test_policy(void)
{
    pad_set_t s;
    pads_init(&s, cap);
    pads_added(&s, 10, 0x28DE, 0x11FF, "Steam Virtual Gamepad", false);
    pads_added(&s, 11, 0x28DE, 0x1205, "Steam Deck", false);
    /* silent devices are never chosen */
    CHECK(!pads_input(&s, 10, false));
    CHECK(!pads_input(&s, 11, false));
    CHECK(s.active == -1);
    /* physical input wins */
    CHECK(pads_input(&s, 11, true));
    CHECK(strstr(last_log, "reason=physical-active") && strstr(last_log, "28de:1205"));
    /* the virtual pad producing input later does not steal it */
    CHECK(!pads_input(&s, 10, true));
    CHECK(s.active == 11);
    /* hotplug: removing the active pad clears it; re-added pad held -> neutral first */
    pads_removed(&s, 11);
    CHECK(s.active == -1);
    pads_added(&s, 12, 0x28DE, 0x1205, "Steam Deck", true);
    CHECK(!pads_input(&s, 12, true)); /* still held from before */
    CHECK(!pads_input(&s, 12, false)); /* neutral */
    CHECK(pads_input(&s, 12, true));
    /* virtual-only case */
    pad_set_t v;
    pads_init(&v, cap);
    pads_added(&v, 20, 0x28DE, 0x11FF, "Steam Virtual Gamepad", false);
    CHECK(pads_input(&v, 20, true));
    CHECK(strstr(last_log, "reason=virtual-only"));
    /* a physical pad's first input re-evaluates */
    pads_added(&v, 21, 0x045E, 0x028E, "Xbox 360 Controller", false);
    CHECK(pads_input(&v, 21, true));
    CHECK(!pads_input(&v, 20, true));
}

/* SDL-level: the Steam hints hide the physical pad until the filter runs. */
static void test_sdl_virtual(void)
{
    setenv("SDL_GAMECONTROLLER_IGNORE_DEVICES", "0x28de/0x1205,0x17ef/0x61eb", 1);
    setenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 1);
    pads_fix_environment(cap);
    CHECK(!strcmp(getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES"), ""));
    CHECK(!strcmp(getenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"), "1"));
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        printf("  skip SDL case: %s\n", SDL_GetError());
        return;
    }
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_VirtualJoystickDesc d;
    memset(&d, 0, sizeof d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.vendor_id = 0x28DE;
    d.product_id = 0x1205;
    d.name = "Steam Deck (virtual test device)";
    int idx = SDL_JoystickAttachVirtualEx(&d);
    CHECK(idx >= 0);
    CHECK(SDL_IsGameController(idx)); /* would be hidden without the filter */
    SDL_GameController *gc = SDL_GameControllerOpen(idx);
    CHECK(gc != NULL);
    if (gc) {
        SDL_Joystick *j = SDL_GameControllerGetJoystick(gc);
        SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 1);
        SDL_GameControllerUpdate();
        CHECK(SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A) == 1);
        pad_set_t s;
        pads_init(&s, cap);
        pads_added(&s, 99, 0x28DE, 0x11FF, "Steam Virtual Gamepad", false); /* silent */
        int32_t id = SDL_JoystickInstanceID(j);
        pads_added(&s, id, SDL_GameControllerGetVendor(gc), SDL_GameControllerGetProduct(gc),
                   SDL_GameControllerName(gc), false);
        CHECK(!pads_input(&s, 99, false));
        CHECK(pads_input(&s, id, SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)));
        CHECK(s.active == id);
        SDL_GameControllerClose(gc);
    }
    SDL_JoystickDetachVirtual(idx);
#else
    printf("  skip SDL virtual device: SDL < 2.24\n");
#endif
    SDL_Quit();
}

int main(void)
{
    printf("filter\n");
    test_filter();
    printf("policy\n");
    test_policy();
    printf("sdl virtual device under Steam hints\n");
    test_sdl_virtual();
    printf(failures ? "FAILED (%d)\n" : "all pad tests passed\n", failures);
    return failures ? 1 : 0;
}
