#define main googleboot_program_main
#include "../googleboot-menu/src/googleboot-menu.c"
#undef main
#include <assert.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    boot_entry_t entries[MAX_ENTRIES];
    int count = load_boot_entries(entries, MAX_ENTRIES, argv[1], true);
    assert(count == 3);
    assert(strcmp(entries[0].luks_dev, "") == 0);
    assert(strcmp(entries[0].action_cmd, "usb-command") == 0);
    assert(strcmp(entries[1].action_cmd, "windows-command") == 0);
    assert(strcmp(entries[2].luks_dev, "/dev/nvme0n1p32") == 0);
    assert(strcmp(entries[2].action_cmd, "linux-command") == 0);
    return 0;
}
