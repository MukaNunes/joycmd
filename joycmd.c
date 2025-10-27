#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_BUTTONS 32
#define MAX_COMBOS 32
#define CONFIG_DIR "/etc/joycmd"
#define CONFIG_FILE "/etc/joycmd/joycmd.conf"

struct combo {
    int buttons[MAX_BUTTONS];
    int count;
    char command[256];
};

/**
 * Reads a joystick event from the joystick device.
 */
int read_event(int fd, struct js_event *event) {
    ssize_t bytes = read(fd, event, sizeof(*event));
    return (bytes == sizeof(*event)) ? 0 : -1;
}

/**
 * Load configuration from /etc/joycmd/joycmd.conf.
 * Returns number of combos loaded.
 */
int load_config(struct combo combos[]) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        perror("Could not open config file");
        return 0;
    }

    char line[512];
    int count = 0;

    while (fgets(line, sizeof(line), f) && count < MAX_COMBOS) {
        if (line[0] == '#' || strlen(line) < 3)
            continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *buttons_str = strtok(line, " ,\t\n");
        char *cmd = eq + 1;

        struct combo c = {.count = 0};
        memset(c.buttons, -1, sizeof(c.buttons));
        memset(c.command, 0, sizeof(c.command));

        // Parse buttons
        char *token = strtok(line, ",");
        while (token && c.count < MAX_BUTTONS) {
            c.buttons[c.count++] = atoi(token);
            token = strtok(NULL, ",");
        }

        // Parse command
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        cmd[strcspn(cmd, "\n")] = '\0';
        strncpy(c.command, cmd, sizeof(c.command) - 1);

        combos[count++] = c;
    }

    fclose(f);
    return count;
}

/**
 * Check if all buttons in a combo are pressed.
 */
int check_combo(const struct combo *c, int *btnState) {
    for (int i = 0; i < c->count; i++) {
        int btn = c->buttons[i];
        if (btn < 0) continue;
        if (!btnState[btn])
            return 0;
    }
    return 1;
}

/**
 * Creates default config file if it doesn't exist.
 */
void ensure_config_exists() {
    struct stat st;

    // Create directory if missing
    if (stat(CONFIG_DIR, &st) == -1) {
        if (mkdir(CONFIG_DIR, 0755) == -1) {
            perror("Could not create /etc/joycmd directory");
            return;
        }
    }

    // Create file if missing
    if (stat(CONFIG_FILE, &st) == -1) {
        FILE *f = fopen(CONFIG_FILE, "w");
        if (!f) {
            perror("Could not create default config file");
            return;
        }

        fprintf(f,
            "# joycmd configuration file\n"
            "# Each line maps a button combination to a shell command.\n"
            "# Format: button1,button2,... = command\n"
            "# Example:\n"
            "# 9,10 = killsteam\n"
            "# 0,1,2 = echo \"Combo secreto!\"\n\n"
        );
        fclose(f);
        printf("Created default config file at %s\n", CONFIG_FILE);
    }
}

int main(int argc, char *argv[]) {
    const char *device = "/dev/input/js0";
    int debug = 0;

    // Argument parsing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) debug = 1;
        else device = argv[i];
    }

    // Ensure config exists
    ensure_config_exists();

    // Load configuration
    struct combo combos[MAX_COMBOS];
    int combo_count = load_config(combos);

    if (combo_count == 0) {
        printf("No valid combos found in %s\n", CONFIG_FILE);
    }

    int js = open(device, O_RDONLY);
    if (js == -1) {
        perror("Could not open joystick");
        return 1;
    }

    struct js_event event;
    int btnState[MAX_BUTTONS] = {0};

    if (debug) printf("joycmd running in debug mode...\n");

    while (read_event(js, &event) == 0) {
        if (event.type == JS_EVENT_BUTTON) {
            btnState[event.number] = event.value;

            if (debug)
                printf("Button %d %s\n", event.number, event.value ? "pressed" : "released");

            // Check all combos
            for (int i = 0; i < combo_count; i++) {
                if (check_combo(&combos[i], btnState)) {
                    if (debug)
                        printf("Executing: %s\n", combos[i].command);
                    system(combos[i].command);
                }
            }
        }
        fflush(stdout);
    }

    close(js);
    return 0;
}
