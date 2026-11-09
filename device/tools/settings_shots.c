/* Host tool: draws Settings pages (Advanced AI, the own server, AI & keys) into a memory framebuffer and saves each as
 * a PNG, for checking layout and wording without a Kindle. Taps go through settings_tap at the centre of a control,
 * found by its hit label (the same labels the e2e tests use). Fonts: DejaVu Sans stands in for Amazon Ember, so line
 * breaks differ a little from the Kindle. No network, no real keys. Usage: device/tools/settings_shots.sh */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "scribe.h"
#include "stb_image_write.h" /* (implemented in brain.c, to memory only) */

/* ---- the rest of the app ---- */
void logf_(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "log: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}
double mono_ms(void) { return 0; }
int net_busy(void) { return 0; }
void page_load(App *a, int n) { (void)a; (void)n; }
void app_key_check(App *a, const char *provider) { ui_set_status(a, "(test %s: not run on the host)", provider); }

static App app;
static const char *out_dir;

static void png_cb(void *ctx, void *data, int size) { fwrite(data, 1, (size_t)size, ctx); }

static void shot(const char *name) {
	char p[512];
	snprintf(p, sizeof p, "%s/%s.png", out_dir, name);
	FILE *f = fopen(p, "wb");
	if (!f) { perror(p); exit(2); }
	stbi_write_png_to_func(png_cb, f, app.W, app.H, 1, app.disp.fb + (size_t)app.disp.yoffset * app.stride, app.stride);
	fclose(f);
	printf("%s  (status: %s)\n", p, app.status);
}

/* Taps the control whose hit label is label (exact), via the developer dump. */
static void tap(const char *label) {
	char p[512], line[512];
	snprintf(p, sizeof p, "%s/hits.txt", out_dir);
	hits_dump(&app, p);
	FILE *f = fopen(p, "r");
	while (f && fgets(line, sizeof line, f)) {
		char kind[16], lab[160];
		int x, y, w, h;
		if (sscanf(line, "%15[^\t]\t%159[^\t]\t%d %d %d %d", kind, lab, &x, &y, &w, &h) == 6 && !strcmp(lab, label)) {
			fclose(f);
			settings_tap(&app, x + w / 2, y + h / 2);
			return;
		}
	}
	if (f) fclose(f);
	fprintf(stderr, "no control '%s' on this page\n", label);
	exit(3);
}

/* What Test & list models would have found on a server */
static void server_list(const char *id, const char *json) {
	KeyCheck *k = brain_key_check_prepare(&app, id, "");
	char msg[200];
	brain_key_check_result(k, 200, json, strlen(json), msg, sizeof msg);
	brain_key_check_free(k);
	settings_refresh(&app);
	ui_set_status(&app, "%s", msg);
}

static void font(const char *file, int style) {
	char p[256];
	snprintf(p, sizeof p, "/usr/share/fonts/truetype/dejavu/%s", file);
	if (disp_font_add(&app.font_ui, p, style) < 0) { fprintf(stderr, "font %s missing\n", p); exit(2); }
}

int main(int argc, char **argv) {
	out_dir = argc > 1 ? argv[1] : ".";
	mkdir(SECURE_DIR, 0700);
	mkdir(USB_DIR, 0755);
	if (disp_open_mem(&app.disp, 1860, 2480, 1872) != 0) return 2;
	app.W = 1860; app.H = 2480; app.stride = 1872;
	app.fb = app.disp.fb;
	app.cx = 0; app.cy = BAR_H; app.cw = app.W; app.ch = app.H - BAR_H;
	font("DejaVuSans.ttf", TXT_REGULAR);
	font("DejaVuSans-Bold.ttf", TXT_BOLD);
	brain_load_config(&app);
	key_set("anthropic_api_key", "sk-ant-test-0123456789abcdefABCD");
	key_set("openai_api_key", "sk-test-0123456789abcdefWXYZ");
	brain_keys_changed(&app);
	snprintf(app.mode_id[0], sizeof app.mode_id[0], "talk");
	app.nmodes = 1;

	settings_open(&app);
	shot("01-home");
	tap("AI");
	shot("02-ai-services");
	tap("ai:custom");
	shot("03-ai-self-hosted-not-set-up");
	tap("ai:server-setup");
	brain_config_apply(&app, "custom_name", "Home Ollama");
	brain_config_apply(&app, "custom_url", "http://10.0.0.20:11434/v1");
	brain_config_apply(&app, "custom_model", "qwen3-vl:8b");
	server_list("custom", "{\"data\":[{\"id\":\"llama3.2:3b\"},{\"id\":\"qwen3-vl:8b\"},{\"id\":\"gemma3:12b\"},"
			      "{\"id\":\"qwen3-vl:30b-a3b-instruct-q4_K_M\"},{\"id\":\"llava:13b\"}]}");
	shot("04-server-1");
	tap("< Back");
	tap("ai:add-server");
	shot("05-server-2-new");
	brain_config_apply(&app, "custom2_name", "Work gateway");
	brain_config_apply(&app, "custom2_url", "https://ai.example.com/v1");
	brain_config_apply(&app, "custom2_model", "gpt-6-astra");
	key_set("custom2_api_key", "sk-work-0123456789abcdWXYZ");
	brain_keys_changed(&app);
	server_list("custom2", "{\"data\":[{\"id\":\"gpt-6-astra\"},{\"id\":\"gpt-6.1-sol\"},{\"id\":\"claude-opus-5-5\"}]}");
	shot("06-server-2");
	tap("< Back");
	tap("ai:anthropic");
	shot("07-ai-services-with-servers");
	tap("ai:tab:features");
	shot("08-each-feature");
	tap("adv:row:math");
	shot("09-warning");
	tap("adv:ok");
	shot("10-math-default");
	tap("adv:svc:custom2");
	shot("11-math-on-server-2");
	tap("adv:tab:tuning");
	tap("adv:effort:high");
	tap("adv:temp:0");
	brain_config_apply(&app, "ai_math_instructions", "I'm learning algebra: explain each step and name the rule you used.");
	settings_refresh(&app);
	shot("12-math-tuning");
	tap("< Back");
	tap("adv:row:typeup");
	tap("adv:svc:custom");
	tap("< Back");
	tap("adv:row:all");
	brain_config_apply(&app, "ai_instructions", "Use British spelling.");
	settings_refresh(&app);
	tap("< Back");
	shot("13-each-feature-custom");
	tap("< Back");
	shot("14-home-after");
	return 0;
}
