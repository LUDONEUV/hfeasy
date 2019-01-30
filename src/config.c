/* HFeasy

Copyright (c) 2019 Luis Alves

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "hfeasy.h"

#define CONFIG_MAGIC_VER1  0xa3
#define CONFIG_OFFSET      0x00
#define CONFIG_SIZE        (sizeof(struct hfeasy_config))

struct hfeasy_state state;
static hftimer_handle_t reset_timer, led_timer;

void USER_FUNC get_module_name(char *buf);

static void USER_FUNC reboot_timer_handler(hftimer_handle_t timer)
{
	hfsys_reset();
}

void USER_FUNC reboot(void)
{
	hftimer_start(reset_timer);
}

static void USER_FUNC led_timer_handler(hftimer_handle_t timer)
{
	static char led = 0;
	
	led ^= 1;
	if (state.mqtt_ready == 0) {
		gpio_set_led(led);
		hftimer_start(timer);
	} else {
		gpio_set_led(0);
	}
}


static const char *config_page =
	"<!DOCTYPE html><html><head><title>HFeasy config v%d.%d</title></head><body>"\
	"<h1>HFeasy config page</h1><hr>"\
	"<h2>Module</h2><br>"\
	"<form action=\"/config\" method=\"GET\">"\
	"Module name: <input type=\"text\" name=\"module_name\" value=\"%s\"><br>"\
	"<input type=\"submit\" value=\"Commit\"></form>"\
	"<h2>MQTT client</h2><br>"\
	"<form action=\"/config\" method=\"GET\">"\
	"Server IP: <input type=\"text\" name=\"mqtt_hostname\" value=\"%s\"><br>"\
	"Server port (0=disabled): <input type=\"text\" name=\"mqtt_port\" value=\"%d\"><br>"\
	"Username: <input type=\"text\" name=\"mqtt_user\" value=\"%s\"><br>"\
	"Password: <input type=\"password\" name=\"mqtt_pass\" value=\"%s\"><br>"\
	"Subscribe topic: <input type=\"text\" name=\"mqtt_sub_topic\" value=\"%s\"><br>"\
	"Publish topic: <input type=\"text\" name=\"mqtt_pub_topic\" value=\"%s\"><br>"\
	"QOS: <input type=\"text\" name=\"mqtt_qos\" value=\"%d\"><br>"\
	"ON value: <input type=\"text\" name=\"mqtt_on_value\" value=\"%s\"><br>"\
	"OFF value: <input type=\"text\" name=\"mqtt_off_value\" value=\"%s\"><br>"\
	"<input type=\"submit\" value=\"Commit values\"></form>"\
	"<hr><form action=\"/config\" method=\"GET\"><input type=\"submit\" value=\"Save and reboot\" name=\"save\"></form>"\
	"</body></html>";

static void USER_FUNC httpd_page_config(char *url, char *rsp)
{
	char tmp[50];
	int ret;
	
	ret = httpd_arg_find(url, "mqtt_hostname", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_server_hostname, tmp);
	
	ret = httpd_arg_find(url, "mqtt_port", tmp);
	if (ret > 0)
		state.cfg.mqtt_server_port = atoi(tmp);

	ret = httpd_arg_find(url, "mqtt_user", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_server_user, tmp);

	ret = httpd_arg_find(url, "mqtt_pass", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_server_pass, tmp);

	ret = httpd_arg_find(url, "mqtt_sub_topic", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_sub_topic, tmp);
		
	ret = httpd_arg_find(url, "mqtt_pub_topic", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_pub_topic, tmp);

	ret = httpd_arg_find(url, "mqtt_qos", tmp);
	if (ret > 0)
		state.cfg.mqtt_qos = atoi(tmp);
	
	ret = httpd_arg_find(url, "mqtt_on_value", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_on_value, tmp);

	ret = httpd_arg_find(url, "mqtt_off_value", tmp);
	if (ret > 0)
		strcpy(state.cfg.mqtt_off_value, tmp);

	
	ret = httpd_arg_find(url, "module_name", tmp);
	if (ret > 0) {
		strcpy(state.cfg.module_name, tmp);
	} else if (state.cfg.module_name[0] == '\0') {
			get_module_name(state.cfg.module_name);
	}
	
	ret = httpd_arg_find(url, "save", tmp);
	if (ret > 0) {
		config_save();
		reboot();
	}
	
	sprintf(rsp, config_page, HFEASY_VERSION_MAJOR, HFEASY_VERSION_MINOR,
					state.cfg.module_name,
					state.cfg.mqtt_server_hostname, state.cfg.mqtt_server_port,
					state.cfg.mqtt_server_user, state.cfg.mqtt_server_pass,
					state.cfg.mqtt_sub_topic, state.cfg.mqtt_pub_topic, state.cfg.mqtt_qos,
					state.cfg.mqtt_on_value, state.cfg.mqtt_off_value);

	u_printf("page_size=%d\r\n", strlen(rsp));
}

static void get_reset_reason(uint32_t r, char *s)
{
	s[0] = '\0';
	
	if (r == HFSYS_RESET_REASON_NORMAL) { // power off restart
		strcat(s, "normal startup");
		return;
	}
	if (r & HFSYS_RESET_REASON_ERESET) // hardware restart
		strcat(s, "reset pin,");
	if (r & HFSYS_RESET_REASON_IRESET0) // soft restart softreset
		strcat(s, "softreset,");
	if (r & HFSYS_RESET_REASON_IRESET1) // call hfsys_reset
		strcat(s, "hfsys_reset,");
	if (r & HFSYS_RESET_REASON_WPS) // WPS restart
		strcat(s, "wps restart,");
	if (r & HFSYS_RESET_REASON_WPS_OK) // wps success
		strcat(s, "wps success,");
	if (r & HFSYS_RESET_REASON_SMARTLINK_START) // turn on smartLink
		strcat(s, "smartlink restart,");
	if (r & HFSYS_RESET_REASON_SMARTLINK_OK) // smartlink success
		strcat(s, "smartlink ok,");
	if (strlen(s) == 0)
		strcat(s, "unknown,");
	s[strlen(s) - 1] = '\0';
	return;
}

static const char *status_page =
	"<!DOCTYPE html><html><head><title>HFeasy status v%d.%d</title></head><body>"\
	"<h1>HF Easy v%d.%d module status</h1><hr>"\
	"<h2>System</h2><br>"\
	"Reset flags: %08x, reason: %s<br>Free memory: %u bytes<br>"\
	"Uptime: %s"\
	"<hr>"\
	"<h2>GPIO status</h2><br>"\
	"Switch: %s<br>Relay: %s<br>"\
	"<hr>"\
	"<h2>Timer status</h2><br>"\
	"Countdown (turn OFF): %s<br>"\
	"Countdown (turn ON): %s<br>"\
	"<hr>"\
	"<h2>Connectivity</h2>"\
	"Hostname: %s<br>"\
	"MQTT server: %s"\
  "</body></html>";

static void USER_FUNC httpd_page_status(char *url, char *rsp)
{
	char cd_off[25], cd_on[25], uptime[25];
	char rr[100];
	uint32_t i, h, m, s, now = hfsys_get_time() / 1000;
	
	get_reset_reason(state.reset_reason, rr);
	
	if (state.cfg.countdown[0] != 0) {
		if (state.countdown[0] == 0) {
			sprintf(cd_off, "waiting for ON state");
		} else {
			i = state.countdown[0] - now;
			s = i % 60;
			i /= 60;
			m = i % 60;
			i /= 60;
			h = i;
			sprintf(cd_off, "%dh%dm%ds", h, m, s);
		}
	} else {
		sprintf(cd_off, "disabled");
	}
	
	if (state.cfg.countdown[1] != 0) {
		if (state.countdown[1] == 0) {
			sprintf(cd_on, "waiting for OFF state");
		} else {
			i = state.countdown[1] - now;
			s = i % 60;
			i /= 60;
			m = i % 60;
			i /= 60;
			h = i;
			sprintf(cd_on, "%dh%dm%ds", h, m, s);
		}
	} else {
		sprintf(cd_on, "disabled");
	}
	
	i = now;
	s = i % 60;
	i /= 60;
	m = i % 60;
	i /= 60;
	h = i % 24;
	i /= 24;
	sprintf(uptime, "%d days %dh%dm%ds", i, h, m, s);
	
	sprintf(rsp, status_page, HFEASY_VERSION_MAJOR, HFEASY_VERSION_MINOR,
					HFEASY_VERSION_MAJOR, HFEASY_VERSION_MINOR,
					state.reset_reason, rr, hfsys_get_memory(), uptime,
					gpio_get_state(GPIO_SWITCH) ? "High" : "Low",
					state.relay_state ? "Closed(On)" : "Open(Off)",
					cd_off, cd_on,
					state.cfg.module_name,
					state.mqtt_ready ? "Connected" : "Disconnected");
}


static int USER_FUNC hfsys_event_callback(uint32_t event_id, void *param)
{
	switch(event_id) {
		case HFE_WIFI_STA_CONNECTED:
			u_printf("wifi sta connected!\r\n");
			hftimer_change_period(led_timer, 600);
			break;
			
		case HFE_WIFI_STA_DISCONNECTED:
			state.has_ip = 0;
			u_printf("wifi sta disconnected!\r\n");
			hftimer_change_period(led_timer, 200);
			break;
			
		case HFE_DHCP_OK:
			{
				uint32_t *p_ip;
				p_ip = (uint32_t*) param;
				HF_Debug(DEBUG_WARN, "dhcp ok %08X!\r\n", *p_ip);
				u_printf("dhcp ok %08X!", *p_ip);
				state.has_ip = 1;
				hftimer_change_period(led_timer, 1000);
			}
			break;
		
		case HFE_SMTLK_OK:
			u_printf("smartlink ok!\r\n");
			break;
			
		case HFE_CONFIG_RELOAD:
			u_printf("reload!\r\n");
			break;
			
		default:
			break;
	}
	return 0;
}

struct hfeasy_state* USER_FUNC config_get_state(void)
{
	return &state;
}

static uint8_t USER_FUNC get_macaddr(void)
{
	char *words[3] = {NULL};
	char rsp[64] = {0};
	char tmp[3], *p;
	int ret = 0;
	int i;

	memset(state.mac_addr, 0, sizeof(state.mac_addr));
	state.mac_addr_s[0] = '\0';
	hfat_send_cmd("AT+WSMAC\r\n", sizeof("AT+WSMAC\r\n"), rsp, 64);
	if (hfat_get_words(rsp, words, 2) > 0) {
		if ((rsp[0]=='+') && (rsp[1]=='o') && (rsp[2]=='k')) {
			u_printf("mac = %s\n", words[1]);
			p = words[1];
			strcpy(state.mac_addr_s, p);
			tmp[2] = '\0';
			for (i = 0; i < 6; i++) {
				memcpy(tmp, p, 2);
				p += 2;
				state.mac_addr[i] = strtol(tmp, NULL, 16);
			}
			/*u_printf("%02x:%02x:%02x:%02x:%02x:%02x",
						(int) state.mac_addr[0], (int) state.mac_addr[1],
						(int) state.mac_addr[2], (int) state.mac_addr[3],
						(int) state.mac_addr[4], (int) state.mac_addr[5]
			);*/
			ret = 1;
		}
	}
	return ret;
}

void USER_FUNC config_save(void)
{
	hffile_userbin_write(CONFIG_OFFSET, (char*) &state.cfg, CONFIG_SIZE);
	u_printf("saving config to flash. size=%d\r\n", CONFIG_SIZE);
}


void USER_FUNC get_module_name(char *buf)
{
	char *words[3] = {NULL};
	char rsp[64] = {0};

	hfat_send_cmd("AT+MID\r\n", sizeof("AT+MID\r\n"), rsp, 64);
	if (hfat_get_words(rsp, words, 2) > 0) {
		if ((rsp[0]=='+') && (rsp[1]=='o') && (rsp[2]=='k')) {
			u_printf("module name = %s\n", words[1]);
			strcpy(buf, words[1]);
			return;
		}
	}
	strcpy(buf, "LPx100");
}

int USER_FUNC set_module_name(void)
{
	//char *words[3] = {NULL};
	char rsp[64] = {0};
	char tmp[50];

	/* module name not setup */
	if (strlen(state.cfg.module_name) == 0)
		return 3;
	
	get_module_name(tmp);
	/* module name already set */
	if (strcmp(tmp, state.cfg.module_name) == 0)
		return 2;
	
	sprintf(tmp, "AT+WRMID=%s\r\n", state.cfg.module_name);
	hfat_send_cmd(tmp, strlen(tmp) + 1, rsp, 64);
	if //((hfat_get_words(rsp, words, 1) > 0) && 
			((rsp[0]=='+') && (rsp[1]=='o') && (rsp[2]=='k'))
			return 1;
	return 0;
}

static void USER_FUNC config_load(uint8_t reset)
{
	memset(&state, 0, sizeof(struct hfeasy_state));
	get_macaddr();
	if (!reset)
		hffile_userbin_read(CONFIG_OFFSET, (char*) &state.cfg, CONFIG_SIZE);
	
	u_printf("loading config from flash. size=%d magic=%02x reset=%d\r\n", CONFIG_SIZE, state.cfg.ver, reset);
	if (state.cfg.ver != CONFIG_MAGIC_VER1) {
		/* init config data */
		memset(&state.cfg, 0, sizeof(struct hfeasy_config));
		state.cfg.ver = CONFIG_MAGIC_VER1;
		mqttcli_initcfg();
		
		hffile_userbin_zero();
		config_save();
	}
	
	set_module_name();
}

void USER_FUNC config_init(void)
{
	config_load(0);
	
	state.reset_reason = hfsys_get_reset_reason();
	
	if(hfsys_register_system_event((hfsys_event_callback_t) hfsys_event_callback) != HF_SUCCESS)
		HF_Debug(DEBUG_ERROR,"error registering system event callback\r\n");

	reset_timer = hftimer_create("reboot", 1000, false, HFTIMER_ID_RESET, reboot_timer_handler, 0);
	led_timer = hftimer_create("led", 400, false, 3, led_timer_handler, 0);
	
	/* register webpages */
	httpd_add_page("/config", httpd_page_config);
	httpd_add_page("/status", httpd_page_status);
}
