#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <poll.h>
#include <time.h>

#define MAX_BUTTONS 32
#define MAX_COMBOS 32
#define MAX_JOYSTICKS 8
#define CONFIG_DIR "/etc/joycmd"
#define CONFIG_FILE "/etc/joycmd/joycmd.conf"

struct combo
{
    int buttons[MAX_BUTTONS];
    int count;
    char command[256];
    int active;
};

struct joystick_device
{
    int fd;
    char name[128];
    int btnState[MAX_BUTTONS];
    struct combo combos[MAX_COMBOS];
    int combo_count;
    char path[64];
};

/* ------------------ BASIC HELPERS ------------------ */

static int read_event(int fd, struct js_event *event)
{
    ssize_t bytes = read(fd, event, sizeof(*event));
    return (bytes == sizeof(*event)) ? 0 : -1;
}

/* ------------------ CONFIG PARSING ------------------ */

static int load_config(const char *joyname, struct combo combos[])
{
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f)
    {
        perror("Could not open config file");
        return 0;
    }

    char line[512];
    int count = 0;
    char current_section[128] = "default";
    int in_matching_section = 0;

    while (fgets(line, sizeof(line), f) && count < MAX_COMBOS)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || strlen(line) < 2)
            continue;

        if (line[0] == '[')
        {
            char section[128];
            if (sscanf(line, "[%127[^]]]", section) == 1)
            {
                strncpy(current_section, section, sizeof(current_section) - 1);
                in_matching_section = (strcasecmp(current_section, joyname) == 0);
            }
            continue;
        }

        if (!in_matching_section && strcasecmp(current_section, "default") != 0)
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *cmd = eq + 1;
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;

        struct combo c = {.count = 0, .active = 0};
        memset(c.buttons, -1, sizeof(c.buttons));
        strncpy(c.command, cmd, sizeof(c.command) - 1);

        char *token = strtok(line, ",");
        while (token && c.count < MAX_BUTTONS)
        {
            c.buttons[c.count++] = atoi(token);
            token = strtok(NULL, ",");
        }

        combos[count++] = c;
    }

    fclose(f);
    return count;
}

/* ------------------ CONFIG CREATION ------------------ */

static void ensure_config_exists(void)
{
    struct stat st;

    if (stat(CONFIG_DIR, &st) == -1 && mkdir(CONFIG_DIR, 0755) == -1)
    {
        perror("Could not create /etc/joycmd directory");
        printf("\n\033[33mPlease, run joycmd as root to create the config file\033[0m\n\n");
        return;
    }

    if (stat(CONFIG_FILE, &st) == -1)
    {
        FILE *f = fopen(CONFIG_FILE, "w");
        if (!f)
        {
            perror("Could not create default config file");
            return;
        }

        fprintf(f,
                "# joycmd configuration file with joystick sections\n"
                "# Each section corresponds to a joystick name as detected by the system.\n"
                "# Format:\n"
                "# [Joystick Name]\n"
                "# button1,button2,... = command\n\n"
                "[default]\n"
                "0,1,2 = echo \"Default combo\"\n\n"
                "[Wireless Controller]\n"
                "9,10 = killapps\n");
        fclose(f);
        printf("Created default config file at %s\n", CONFIG_FILE);
    }
}

/* ------------------ JOYSTICK MANAGEMENT ------------------ */

static int open_joystick(const char *device, struct joystick_device *joy, int debug)
{
    joy->fd = open(device, O_RDONLY | O_NONBLOCK);
    if (joy->fd < 0)
        return -1;

    strncpy(joy->path, device, sizeof(joy->path) - 1);

    if (ioctl(joy->fd, JSIOCGNAME(sizeof(joy->name)), joy->name) < 0)
        strncpy(joy->name, "Unknown", sizeof(joy->name));

    printf("Joystick connected: %s (%s)\n", joy->name, device);

    memset(joy->btnState, 0, sizeof(joy->btnState));
    joy->combo_count = load_config(joy->name, joy->combos);

    if (debug)
        printf("Loaded %d combos for joystick '%s'\n", joy->combo_count, joy->name);

    return 0;
}

/* ------------------ HELPERS ------------------ */

static void print_help(const char *progname)
{
    printf("Usage: %s [options]\n\n", progname);
    printf("Options:\n");
    printf("  -d              Enable debug mode (shows button presses)\n");
    printf("  -h, --help      Show this help message and exit\n\n");
    printf("Configuration file:\n  %s\n", CONFIG_FILE);
}

/* ------------------ JOYSTICK SLOTS ------------------ */

static struct joystick_device joys[MAX_JOYSTICKS];
static int joy_count = 0;

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_JOYSTICKS; i++)
        if (joys[i].fd <= 0)
            return i;
    return -1;
}

static void remove_joystick(int slot)
{
    if (joys[slot].fd > 0)
    {
        printf("Joystick disconnected: %s (%s)\n", joys[slot].name, joys[slot].path);
        close(joys[slot].fd);
        memset(&joys[slot], 0, sizeof(struct joystick_device));
        joy_count--;
    }
}

static void add_joystick(const char *path, int debug)
{
    int slot = find_free_slot();
    if (slot < 0)
        return;

    if (open_joystick(path, &joys[slot], debug) == 0)
        joy_count++;
}

/* ------------------ MAIN LOOP ------------------ */

int main(int argc, char *argv[])
{
    int debug = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            print_help(argv[0]);
            return 0;
        }
        else if (!strcmp(argv[i], "-d"))
        {
            debug = 1;
        }
    }

    ensure_config_exists();
    printf("Scanning for joysticks...\n");

    for (int i = 0; i < MAX_JOYSTICKS; i++)
    {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/js%d", i);
        if (access(path, F_OK) == 0)
            add_joystick(path, debug);
    }

    printf("%d joystick(s) active.\n", joy_count);

    while (1)
    {
        static time_t last_scan = 0;
        time_t now = time(NULL);
        if (now - last_scan >= 2)
        {
            last_scan = now;
            for (int i = 0; i < MAX_JOYSTICKS; i++)
            {
                char path[32];
                snprintf(path, sizeof(path), "/dev/input/js%d", i);

                int already_open = 0;
                for (int j = 0; j < MAX_JOYSTICKS; j++)
                    if (joys[j].fd > 0 && !strcmp(joys[j].path, path))
                        already_open = 1;

                if (!already_open && access(path, F_OK) == 0)
                    add_joystick(path, debug);
            }
        }

        struct pollfd pfds[MAX_JOYSTICKS];
        int active = 0;
        for (int i = 0; i < MAX_JOYSTICKS; i++)
            if (joys[i].fd > 0)
            {
                pfds[active].fd = joys[i].fd;
                pfds[active].events = POLLIN;
                active++;
            }

        if (poll(pfds, active, 500) < 0)
            continue;

        for (int i = 0; i < MAX_JOYSTICKS; i++)
        {
            if (joys[i].fd <= 0)
                continue;

            struct js_event e;
            while (read_event(joys[i].fd, &e) == 0)
            {
                if (e.type != JS_EVENT_BUTTON)
                    continue;

                joys[i].btnState[e.number] = e.value;

                if (debug)
                    printf("[%s] Button %d %s\n", joys[i].name, e.number,
                           e.value ? "pressed" : "released");

                // ------------------ COMBO CHECK ------------------
                for (int c = 0; c < joys[i].combo_count; c++)
                {
                    int allPressed = 1;
                    for (int b = 0; b < joys[i].combos[c].count; b++)
                    {
                        int btn = joys[i].combos[c].buttons[b];
                        if (btn < 0)
                            continue;
                        if (!joys[i].btnState[btn])
                        {
                            allPressed = 0;
                            break;
                        }
                    }

                    if (allPressed && !joys[i].combos[c].active)
                    {
                        printf("[%s] Executing: %s\n",
                               joys[i].name, joys[i].combos[c].command);
                        system(joys[i].combos[c].command);

                        joys[i].combos[c].active = 0;
                        for (int d = 0; d < joys[i].combos[c].count; d++)
                        {
                            int comboButton = joys[i].combos[c].buttons[d];
                            joys[i].btnState[comboButton] = 0;
                        }
                    }
                }
            }

            if (errno == ENODEV)
                remove_joystick(i);
        }

        fflush(stdout);
    }

    return 0;
}
