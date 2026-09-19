/* See LICENSE file for copyright and license details. */

/* interval between updates (in ms) */
const unsigned int interval = 1000;

/* text to show if no value can be retrieved */
static const char unknown_str[] = "n/a";

/* maximum command output length */
#define CMDLEN 128

#if defined(__linux__)
#include <dirent.h>
#endif

/* temperature sensor */
static const char temp_sensor[] = "/sys/class/hwmon/hwmon7/temp1_input";

/* current TLP power mode */
static const char tlp_mode_cmd[] =
	"tlp-stat -s 2>/dev/null | "
	"sed -n 's/^Mode[[:space:]]*=[[:space:]]*//p'";

/*
 * Dynamic Nerd Font icons.
 *
 * Keep the data-reading components separate from the presentation logic so
 * that changing an icon set does not require changes to slstatus.c.
 */
static const char *clock_icons[] = {
	/* 12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 o'clock */
	"󱐿", "󱑀", "󱑁", "󱑂", "󱑃", "󱑄",
	"󱑅", "󱑆", "󱑇", "󱑈", "󱑉", "󱑊",
};

static const char *battery_icons[] = {
	/* alert/empty, 10, 20, ..., 90, full */
	"󰂃", "󰁺", "󰁻", "󰁼", "󰁽", "󰁾",
	"󰁿", "󰂀", "󰂁", "󰂂", "󰁹",
};

static const char *battery_charging_icons[] = {
	/* charging, 10, 20, ..., 90, 100 */
	"󰂄", "󰢜", "󰂆", "󰂇", "󰂈", "󰢝",
	"󰂉", "󰢞", "󰂊", "󰂋", "󰂄",
};

static const char *wifi_icons[] = {
	/* disconnected, 1, 2, 3, 4 bars */
	"󰤮", "󰤟", "󰤢", "󰤥", "󰤨",
};

static const char *volume_icons[] = {
	/* muted, low, medium, high */
	"󰝟", "󰕿", "󰖀", "󰕾",
};

static int
parse_percent(const char *value, int *percent)
{
	char *end;
	long n;

	if (!value || !*value)
		return 0;

	n = strtol(value, &end, 10);
	if (end == value)
		return 0;
	while (*end == ' ' || *end == '\t')
		end++;
	if (*end == '%')
		end++;
	while (*end == ' ' || *end == '\t')
		end++;
	if (*end != '\0')
		return 0;

	if (n < 0)
		n = 0;
	if (n > 100)
		n = 100;
	*percent = (int)n;

	return 1;
}

static const char *
datetime_icon(const char *unused)
{
	time_t now;
	struct tm *tm;
	char date[64], clock[64];

	now = time(NULL);
	if (!(tm = localtime(&now)) ||
	    !strftime(date, sizeof(date), "%F", tm) ||
	    !strftime(clock, sizeof(clock), "%T", tm))
		return NULL;

	return bprintf(" %s %s %s", date,
	               clock_icons[tm->tm_hour % LEN(clock_icons)], clock);
}

static int
adapter_online(void)
{
#if defined(__linux__)
	DIR *dir;
	FILE *fp;
	struct dirent *entry;
	char path[256];
	int online;

	if (!(dir = opendir("/sys/class/power_supply")))
		return 0;

	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		if (esnprintf(path, sizeof(path),
		              "/sys/class/power_supply/%s/online", entry->d_name) < 0)
			continue;
		if (!(fp = fopen(path, "r")))
			continue;

		online = 0;
		if (fscanf(fp, "%d", &online) == 1 && online > 0) {
			fclose(fp);
			closedir(dir);
			return 1;
		}
		fclose(fp);
	}

	closedir(dir);
#endif
	return 0;
}

static const char *
battery_display(const char *bat)
{
	const char *value, *state, *const *icons;
	int percent, index, plugged;

	value = battery_perc(bat);
	if (!parse_percent(value, &percent))
		return NULL;
	state = battery_state(bat);
	plugged = state && !strcmp(state, "+");
	if (!plugged)
		plugged = adapter_online();
	icons = plugged ? battery_charging_icons : battery_icons;

	index = (percent + 5) / 10;
	if (index >= (int)LEN(battery_icons))
		index = LEN(battery_icons) - 1;

	return bprintf("%s %d%%", icons[index], percent);
}

static const char *
wifi_display(const char *interface)
{
	const char *value, *essid;
	char ssid[128];
	int percent, index;

	value = wifi_perc(interface);
	if (!parse_percent(value, &percent))
		return bprintf("%s offline", wifi_icons[0]);

	essid = wifi_essid(interface);
	if (!essid)
		essid = "unknown";
	if (esnprintf(ssid, sizeof(ssid), "%s", essid) < 0)
		return NULL;

	if (percent == 0)
		index = 0;
	else
		index = (percent - 1) / 25 + 1;
	if (index >= (int)LEN(wifi_icons))
		index = LEN(wifi_icons) - 1;

	return bprintf("%s %s:%d%%", wifi_icons[index], ssid, percent);
}

static const char *
volume_display(const char *cmd)
{
	const char *value;
	int percent, index;

	value = run_command(cmd);
	if (!value)
		return NULL;
	if (!strcmp(value, "MUTE") || !strcmp(value, "MUTED"))
		return bprintf("%s MUTE", volume_icons[0]);
	if (!parse_percent(value, &percent))
		return NULL;

	if (percent == 0)
		index = 0;
	else if (percent <= 33)
		index = 1;
	else if (percent <= 66)
		index = 2;
	else
		index = 3;

	return bprintf("%s %d%%", volume_icons[index], percent);
}

/*
 * function            description                     argument (example)
 *
 * battery_perc        battery percentage              battery name (BAT0)
 *                                                     NULL on OpenBSD/FreeBSD
 * battery_remaining   battery remaining HH:MM         battery name (BAT0)
 *                                                     NULL on OpenBSD/FreeBSD
 * battery_state       battery charging state          battery name (BAT0)
 *                                                     NULL on OpenBSD/FreeBSD
 * cat                 read arbitrary file             path
 * cpu_freq            cpu frequency in MHz            NULL
 * cpu_perc            cpu usage in percent            NULL
 * datetime            date and time                   format string (%F %T)
 * disk_free           free disk space in GB           mountpoint path (/)
 * disk_perc           disk usage in percent           mountpoint path (/)
 * disk_total          total disk space in GB          mountpoint path (/)
 * disk_used           used disk space in GB           mountpoint path (/)
 * entropy             available entropy               NULL
 * gid                 GID of current user             NULL
 * hostname            hostname                        NULL
 * ipv4                IPv4 address                    interface name (eth0)
 * ipv6                IPv6 address                    interface name (eth0)
 * kernel_release      `uname -r`                      NULL
 * keyboard_indicators caps/num lock indicators        format string (c?n?)
 *                                                     see keyboard_indicators.c
 * keymap              layout (variant) of current     NULL
 *                     keymap
 * load_avg            load average                    NULL
 * netspeed_rx         receive network speed           interface name (wlan0)
 * netspeed_tx         transfer network speed          interface name (wlan0)
 * num_files           number of files in a directory  path
 *                                                     (/home/foo/Inbox/cur)
 * ram_free            free memory in GB               NULL
 * ram_perc            memory usage in percent         NULL
 * ram_total           total memory size in GB         NULL
 * ram_used            used memory in GB               NULL
 * run_command         custom shell command            command (echo foo)
 * swap_free           free swap in GB                 NULL
 * swap_perc           swap usage in percent           NULL
 * swap_total          total swap size in GB           NULL
 * swap_used           used swap in GB                 NULL
 * temp                temperature in degree celsius   sensor file
 *                                                     (/sys/class/thermal/...)
 *                                                     NULL on OpenBSD
 *                                                     thermal zone on FreeBSD
 *                                                     (tz0, tz1, etc.)
 * uid                 UID of current user             NULL
 * up                  interface is running            interface name (eth0)
 * uptime              system uptime                   NULL
 * username            username of current user        NULL
 * vol_perc            OSS/ALSA volume in percent      mixer file (/dev/mixer)
 *                                                     NULL on OpenBSD/FreeBSD
 * wifi_essid          WiFi ESSID                      interface name (wlan0)
 * wifi_perc           WiFi signal in percent          interface name (wlan0)
 */
static const struct arg args[] = {
	/* function         format          argument        turn        signal */

	{ cpu_freq,         "| 󰻠 %s/",      NULL,           1,          -1 },
	{ cpu_perc,         "%s%% ",        NULL,           1,          -1 },
	{ ram_perc,         "󰍛 %s%%:",      NULL,           2,          -1 },
	{ swap_perc,        "%s%% ",        NULL,           8,          -1 },
	{ temp,             "󰔄 %s ",        temp_sensor,    1,          -1 },
	{ run_command,      "󰐥 %s | ",      tlp_mode_cmd,   32,         -1 },
	{ disk_perc,        "󰋊 %s%% ",      "/",            32,         -1 },
	{ battery_display,  "%s ",          "BAT1",         16,         -1 },
	{ volume_display,   "%s ",          "sl-volume",    0,          1  },
	{ wifi_display,     "%s | ",        "wlp4s0",       8,          -1 },
	{ run_command,      "󰌌 %s | ",      "sl-fcitx",     0,          2  },
	{ datetime_icon,    "%s |",         NULL,           1,          -1 },
};

/* maximum output string length */
#define MAXLEN CMDLEN * LEN(args)
