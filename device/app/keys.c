/* Keys and passwords (AI providers, the email account) in SECURE_DIR/keys.txt: root-only, one name=value per line.
 * Names: anthropic_api_key, openai_api_key, gemini_api_key, bedrock_api_key, xai_api_key, mistral_api_key,
 * openrouter_api_key, email_password, ... Values never go to the USB storage, the log or a command line. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "scribe.h"

#define MAXKEYS 24

static struct {
	char name[40];
	char value[320];
} K[MAXKEYS];
static int nk;

static void trim(char *s) {
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
	size_t i = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	if (i) memmove(s, s + i, strlen(s + i) + 1);
}

void keys_load(void) {
	nk = 0;
	FILE *f = fopen(SECURE_DIR "/keys.txt", "r");
	if (!f) return;
	char line[400];
	while (fgets(line, sizeof line, f) && nk < MAXKEYS) {
		trim(line);
		char *eq = strchr(line, '=');
		if (!line[0] || line[0] == '#' || !eq) continue;
		*eq = 0;
		trim(line);
		char *v = eq + 1;
		trim(v);
		if (!v[0]) continue;
		snprintf(K[nk].name, sizeof K[nk].name, "%s", line);
		snprintf(K[nk].value, sizeof K[nk].value, "%s", v);
		nk++;
	}
	fclose(f);
}

void keys_save(void) {
	unlink(SECURE_DIR "/keys.tmp");
	int fd = open(SECURE_DIR "/keys.tmp", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!f) {
		if (fd >= 0) close(fd);
		logf_("could not save keys");
		return;
	}
	fchmod(fd, 0600); /* (the umask can only take bits away) */
	for (int i = 0; i < nk; i++)
		if (K[i].value[0]) fprintf(f, "%s=%s\n", K[i].name, K[i].value);
	fflush(f);
	fsync(fd);
	fclose(f);
	rename(SECURE_DIR "/keys.tmp", SECURE_DIR "/keys.txt");
}

/* "" when not set. */
const char *key_get(const char *name) {
	for (int i = 0; i < nk; i++)
		if (!strcmp(K[i].name, name)) return K[i].value;
	return "";
}

/* Sets (or with "" removes) a key and saves the file. */
void key_set(const char *name, const char *value) {
	int i;
	for (i = 0; i < nk; i++)
		if (!strcmp(K[i].name, name)) break;
	if (i == nk) {
		if (!value[0] || nk >= MAXKEYS) return;
		snprintf(K[nk].name, sizeof K[nk].name, "%s", name);
		nk++;
	}
	snprintf(K[i].value, sizeof K[i].value, "%s", value);
	if (!value[0]) { /* drop the entry */
		K[i] = K[nk - 1];
		nk--;
	}
	keys_save();
}

/* For showing a key without revealing it: "sk-ant-...4ZtA" (or "" when not set). */
void key_masked(const char *name, char *out, size_t n) {
	const char *v = key_get(name);
	size_t len = strlen(v);
	if (!len) { snprintf(out, n, "%s", ""); return; }
	if (len <= 10) { snprintf(out, n, "%.2s...", v); return; }
	const char *dash = strchr(v, '-');
	int head = dash && dash - v < 8 ? (int)(dash - v) + 1 : 3;
	if (dash && strchr(dash + 1, '-') && strchr(dash + 1, '-') - v < 12) head = (int)(strchr(dash + 1, '-') - v) + 1;
	snprintf(out, n, "%.*s...%s", head, v, v + len - 4);
}
