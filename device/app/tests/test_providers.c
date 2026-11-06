/* Host tests for the AI providers (brain.c + providers.c + keys.c): config and key-store migration, the request each
 * provider gets, parsing each vendor's documented response shape into the Reply fields, error and refusal handling,
 * the "Test key" calls, and the fake_response.json developer hook. No network: nothing here calls a real API, and
 * every key is a made-up test value. Run by tests/run_provider_tests.sh (which sets SECURE_DIR and USB_DIR). */
#include "../brain.c"

#include <dirent.h>
#include <stdarg.h>

/* ---- stubs for the rest of the app ---- */
static char status[512];
void ui_set_status(App *a, const char *fmt, ...) {
	(void)a;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(status, sizeof status, fmt, ap);
	va_end(ap);
}
void logf_(const char *fmt, ...) { (void)fmt; }
int settings_apply(App *a, const char *k, const char *v) { (void)a; (void)k; (void)v; return 0; }
double mono_ms(void) { return 0; }
int net_busy(void) { return 0; }

static int fails, checks;
#define CHECK(c, ...)                                                                     \
	do {                                                                              \
		checks++;                                                                 \
		if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
	} while (0)

static App app;

static void write_text(const char *path, const char *s) {
	FILE *f = fopen(path, "w");
	if (!f) { perror(path); exit(2); }
	fputs(s, f);
	fclose(f);
}

static int file_has(const char *path, const char *needle) {
	char *s = read_file(path, NULL);
	int r = s && strstr(s, needle);
	free(s);
	return r;
}

/* the value at a dotted path ("a.b.0.c") */
static cJSON *at(cJSON *o, const char *path) {
	char buf[256], *save = NULL;
	snprintf(buf, sizeof buf, "%s", path);
	for (char *t = strtok_r(buf, ".", &save); t && o; t = strtok_r(NULL, ".", &save))
		o = (*t >= '0' && *t <= '9') ? cJSON_GetArrayItem(o, atoi(t)) : cJSON_GetObjectItemCaseSensitive(o, t);
	return o;
}
static const char *at_s(cJSON *o, const char *path) {
	const char *s = cJSON_GetStringValue(at(o, path));
	return s ? s : "";
}

static AiJob *make_job(int prov, const char *mode) {
	AiJob *j = calloc(1, sizeof *j);
	const Provider *p = provider_by_prov(prov);
	j->provider = prov;
	snprintf(j->model, sizeof j->model, "%s", !strcmp(p->default_model, "fast") ? "claude-haiku-5-5" : p->default_model);
	snprintf(j->key, sizeof j->key, "test-key-%s-0123456789", p->id);
	snprintf(j->mode, sizeof j->mode, "%s", mode);
	snprintf(j->region, sizeof j->region, "eu-west-1");
	snprintf(j->voice, sizeof j->voice, "friendly");
	snprintf(j->length, sizeof j->length, "medium");
	snprintf(j->math, sizeof j->math, "hints");
	snprintf(j->lang, sizeof j->lang, "match");
	snprintf(j->target, sizeof j->target, "French");
	j->w = 40;
	j->h = 30;
	j->px = calloc((size_t)j->w * (size_t)j->h, 1);
	memset(j->px, 0xFF, (size_t)j->w * (size_t)j->h);
	j->bx0 = 2; j->by0 = 3; j->bx1 = 20; j->by1 = 25;
	j->page = 3;
	j->history = strdup("[{\"role\":\"user\",\"text\":\"[page 2, talk mode] hello\"},{\"role\":\"assistant\",\"text\":\"Hi! How are you?\"}]");
	return j;
}

static const char INNER[] = "{\"read\":\"what is 2+2\",\"mode\":\"math\",\"style\":\"typeset\",\"reply\":\"Check the sum: 2 + 2 = 4.\","
			    "\"memo\":\"asked about sums\",\"illustrate\":\"a cat sitting, looking at you\"}";
/* the drawing pass: two strokes, the second's point outside the canvas (clamped to 1000,0) */
static const char DRAW_INNER[] = "{\"strokes\":[[[100,100],[200,200],[300,100]],[[1200,-5]]]}";
static const char EMAIL_INNER[] = "{\"subject\":\"Shopping list\",\"body\":\"- eggs\\n- milk\"}";

static char *quoted(const char *s) { /* s as a JSON string literal */
	cJSON *o = cJSON_CreateString(s);
	char *q = cJSON_PrintUnformatted(o);
	cJSON_Delete(o);
	return q;
}

/* the documented response shapes, with the answer text dropped in */
static char *response_for(int api, const char *variant, const char *inner) {
	cJSON *in = cJSON_Parse(inner);
	char *esc = quoted(inner);
	char *out = malloc(strlen(inner) * 2 + 4096);
	switch (api) {
	case API_ANTHROPIC: /* Messages API */
		sprintf(out, "{\"id\":\"msg_01XFDUDYJgAACzvnptvVoYEL\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-haiku-5-5\","
			     "\"content\":[{\"type\":\"text\",\"text\":%s}],\"stop_reason\":\"%s\",\"stop_sequence\":null,"
			     "\"usage\":{\"input_tokens\":2095,\"output_tokens\":503}}", esc, variant ? variant : "end_turn");
		break;
	case API_OPENAI_RESPONSES: /* Responses API: a reasoning item, then the message */
		sprintf(out, "{\"id\":\"resp_67ccd3a9da748190baa7f1570fe91ac604becb25c45c1d41\",\"object\":\"response\",\"status\":\"completed\","
			     "\"output\":[{\"type\":\"reasoning\",\"id\":\"rs_1\",\"summary\":[]},{\"type\":\"message\",\"id\":\"msg_67ccd3acc8d48190a77525dc6de64b4104becb25c45c1d41\","
			     "\"status\":\"completed\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":%s,\"annotations\":[]}]}],"
			     "\"usage\":{\"input_tokens\":36,\"output_tokens\":87,\"total_tokens\":123}}", esc);
		break;
	case API_GEMINI: /* generateContent; Gemini 3 parts carry a thoughtSignature */
		if (variant && !strcmp(variant, "blocked"))
			sprintf(out, "{\"promptFeedback\":{\"blockReason\":\"SAFETY\"},\"usageMetadata\":{\"promptTokenCount\":8}}");
		else
			sprintf(out, "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":%s,\"thoughtSignature\":\"CiQB0e2Kb...\"}],\"role\":\"model\"},"
				     "\"finishReason\":\"STOP\",\"index\":0}],\"usageMetadata\":{\"promptTokenCount\":1290,\"candidatesTokenCount\":61,"
				     "\"totalTokenCount\":1351},\"modelVersion\":\"gemini-3.8-flash\",\"responseId\":\"mBFjaZ6-IJ2m\"}", esc);
		break;
	case API_BEDROCK: { /* Converse: toolUse (Nova) or text after a reasoning block (Claude) */
		char *inj = cJSON_PrintUnformatted(in);
		if (variant && !strcmp(variant, "text"))
			sprintf(out, "{\"output\":{\"message\":{\"role\":\"assistant\",\"content\":[{\"reasoningContent\":{\"reasoningText\":"
				     "{\"text\":\"The user wrote a sum.\",\"signature\":\"sig\"}}},{\"text\":%s}]}},\"stopReason\":\"end_turn\","
				     "\"usage\":{\"inputTokens\":1870,\"outputTokens\":96,\"totalTokens\":1966},\"metrics\":{\"latencyMs\":1830}}", esc);
		else if (variant && !strcmp(variant, "guardrail"))
			sprintf(out, "{\"output\":{\"message\":{\"role\":\"assistant\",\"content\":[{\"text\":\"Sorry, the model cannot answer this question.\"}]}},"
				     "\"stopReason\":\"guardrail_intervened\",\"usage\":{\"inputTokens\":10,\"outputTokens\":9,\"totalTokens\":19}}");
		else
			sprintf(out, "{\"output\":{\"message\":{\"role\":\"assistant\",\"content\":[{\"toolUse\":{\"toolUseId\":\"tooluse_kZJMlvQmRJ6eAyJE5GIl7Q\","
				     "\"name\":\"scribe_reply\",\"input\":%s}}]}},\"stopReason\":\"tool_use\",\"usage\":{\"inputTokens\":1622,"
				     "\"outputTokens\":88,\"totalTokens\":1710},\"metrics\":{\"latencyMs\":1201}}", inj);
		free(inj);
		break;
	}
	default: /* Chat Completions */
		if (variant && !strcmp(variant, "chunks")) /* Mistral content chunks */
			sprintf(out, "{\"id\":\"cmpl-e5cc70bb28c444948073e77776eb30ef\",\"object\":\"chat.completion\",\"model\":\"mistral-large-4\","
				     "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\",\"thinking\":"
				     "[{\"type\":\"text\",\"text\":\"Reading the page.\"}]},{\"type\":\"text\",\"text\":%s}]},\"finish_reason\":\"stop\"}],"
				     "\"usage\":{\"prompt_tokens\":16,\"completion_tokens\":34,\"total_tokens\":50}}", esc);
		else if (variant && !strcmp(variant, "fenced")) { /* prompt-only JSON wrapped in a code fence */
			char *f = malloc(strlen(inner) + 32);
			sprintf(f, "```json\n%s\n```", inner);
			char *fe = quoted(f);
			sprintf(out, "{\"id\":\"gen-1\",\"object\":\"chat.completion\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
				     "\"content\":%s},\"finish_reason\":\"stop\"}]}", fe);
			free(f);
			free(fe);
		} else if (variant && !strcmp(variant, "refusal"))
			sprintf(out, "{\"id\":\"chatcmpl-1\",\"object\":\"chat.completion\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
				     "\"content\":null,\"refusal\":\"I can't help with that.\"},\"finish_reason\":\"stop\"}]}");
		else
			sprintf(out, "{\"id\":\"0daf962f-a275-4a3c-839a-047854645532\",\"object\":\"chat.completion\",\"created\":1762400000,"
				     "\"model\":\"grok-4.3\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":%s,\"refusal\":null},"
				     "\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":1690,\"completion_tokens\":70,\"total_tokens\":1760}}", esc);
	}
	free(esc);
	cJSON_Delete(in);
	return out;
}

static void reply_clear(Reply *r) {
	free(r->reply);
	free(r->slen);
	free(r->dpts);
	memset(r, 0, sizeof *r);
}

static void check_parse(const char *what, int prov, const char *variant) {
	AiJob *j = make_job(prov, "auto");
	if (prov == PROV_BEDROCK && variant && !strcmp(variant, "text")) snprintf(j->model, sizeof j->model, "global.anthropic.claude-haiku-5-5");
	char *body = response_for(provider_by_prov(prov)->api, variant, INNER);
	Reply r = { 0 };
	int ok = brain_parse(&app, j, 200, body, strlen(body), &r);
	CHECK(ok, "%s: parse failed (%s)", what, status);
	if (ok) {
		CHECK(!strcmp(r.reply, "Check the sum: 2 + 2 = 4."), "%s: reply %s", what, r.reply);
		CHECK(!strcmp(r.transcript, "what is 2+2"), "%s: read %s", what, r.transcript);
		CHECK(!strcmp(r.mode, "math") && !strcmp(r.style, "typeset"), "%s: mode/style %s/%s", what, r.mode, r.style);
		CHECK(!strcmp(r.memo, "asked about sums"), "%s: memo %s", what, r.memo);
		CHECK(!strcmp(r.illustrate, "a cat sitting, looking at you") && r.nstrokes == 0, "%s: illustrate %s", what, r.illustrate);
	}
	/* the drawing pass reads the same responses */
	char *db = response_for(provider_by_prov(prov)->api, variant, DRAW_INNER);
	Reply d = { 0 };
	int dok = brain_parse_draw(j, 200, db, strlen(db), &d);
	CHECK(dok && d.nstrokes == 2 && d.slen[0] == 3 && d.slen[1] == 1 && d.dpts[2] == 200.0f && d.dpts[6] == 1000.0f &&
	      d.dpts[7] == 0.0f, "%s: drawing pass strokes", what);
	reply_clear(&d);
	free(db);
	/* the email step reads the same responses */
	char *eb = response_for(provider_by_prov(prov)->api, variant, EMAIL_INNER);
	cJSON *root = cJSON_Parse(eb);
	int refused = 0;
	char *inner = answer_text(prov, root, &refused);
	cJSON *ej = inner ? cJSON_Parse(inner) : NULL;
	CHECK(!strcmp(at_s(ej, "subject"), "Shopping list") && !strcmp(at_s(ej, "body"), "- eggs\n- milk"), "%s: email answer", what);
	cJSON_Delete(ej);
	free(inner);
	cJSON_Delete(root);
	free(eb);
	reply_clear(&r);
	free(body);
	brain_job_free(j);
}

static void check_refusal(const char *what, int prov, const char *variant) {
	AiJob *j = make_job(prov, "talk");
	char *body = response_for(provider_by_prov(prov)->api, variant, INNER);
	Reply r = { 0 };
	int ok = brain_parse(&app, j, 200, body, strlen(body), &r);
	CHECK(!ok && strstr(status, "declined"), "%s: refusal -> %s", what, status);
	reply_clear(&r);
	free(body);
	brain_job_free(j);
}

static void check_error(const char *what, int prov, int st, const char *body, const char *expect) {
	AiJob *j = make_job(prov, "talk");
	Reply r = { 0 };
	int ok = brain_parse(&app, j, st, body, strlen(body), &r);
	CHECK(!ok && strstr(status, expect), "%s: status '%s' (wanted '%s')", what, status, expect);
	brain_job_free(j);
}

static const char *out_dir;

static void check_request(int prov, const char *mode) {
	AiJob *j = make_job(prov, mode);
	const Provider *p = provider_by_prov(prov);
	int email = !strcmp(mode, "email");
	char *req = build_request(j, "iVBORw0KGgoAAAANSUhEUg==");
	cJSON *r = cJSON_Parse(req);
	CHECK(r != NULL, "%s: request is not JSON", p->id);
	char path[512], url[384], hdr[768];
	snprintf(path, sizeof path, "%s/request-%s-%s.json", out_dir, p->id, mode);
	write_text(path, req);
	request_target(j, url, sizeof url, hdr, sizeof hdr);
	const char *schema_prop = email ? "subject" : "illustrate";
	switch (p->api) {
	case API_ANTHROPIC:
		CHECK(!strcmp(url, "https://api.anthropic.com/v1/messages"), "anthropic url %s", url);
		CHECK(strstr(hdr, "x-api-key: test-key-anthropic") != NULL, "anthropic header");
		CHECK(!strcmp(at_s(r, "messages.2.content.0.source.data"), "iVBORw0KGgoAAAANSUhEUg=="), "anthropic image");
		CHECK(at(r, "output_config.format.schema.properties") && at(at(r, "output_config.format.schema.properties"), schema_prop), "anthropic schema");
		CHECK(strlen(at_s(r, "system")) > 100, "anthropic system");
		break;
	case API_OPENAI_RESPONSES:
		CHECK(!strcmp(url, "https://api.openai.com/v1/responses"), "openai url %s", url);
		CHECK(!strncmp(at_s(r, "input.2.content.1.image_url"), "data:image/png;base64,iVBOR", 27), "openai image");
		CHECK(at(at(r, "text.format.schema.properties"), schema_prop) != NULL, "openai schema");
		break;
	case API_GEMINI:
		CHECK(!strcmp(url, "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash:generateContent"), "gemini url %s", url);
		CHECK(strstr(hdr, "x-goog-api-key: test-key-gemini") != NULL, "gemini header");
		CHECK(!strcmp(at_s(r, "contents.1.role"), "model") && !strcmp(at_s(r, "contents.0.role"), "user"), "gemini roles");
		CHECK(!strcmp(at_s(r, "contents.2.parts.0.inlineData.mimeType"), "image/png") &&
		      !strcmp(at_s(r, "contents.2.parts.0.inlineData.data"), "iVBORw0KGgoAAAANSUhEUg=="), "gemini image");
		CHECK(!strcmp(at_s(r, "generationConfig.responseMimeType"), "application/json"), "gemini mime");
		CHECK(at(at(r, "generationConfig.responseJsonSchema.properties"), schema_prop) != NULL, "gemini schema");
		CHECK(!strcmp(at_s(r, "generationConfig.thinkingConfig.thinkingLevel"), email || !strcmp(mode, "talk") ? "LOW" : "MEDIUM"), "gemini thinking");
		CHECK(strlen(at_s(r, "systemInstruction.parts.0.text")) > 100 && !at(r, "model"), "gemini system / no model field");
		break;
	case API_BEDROCK:
		CHECK(!strcmp(url, "https://bedrock-runtime.eu-west-1.amazonaws.com/model/global.amazon.nova-2-lite-v1%3A0/converse"), "bedrock url %s", url);
		CHECK(strstr(hdr, "Authorization: Bearer test-key-bedrock") != NULL, "bedrock header");
		CHECK(!strcmp(at_s(r, "messages.2.content.0.image.format"), "png") &&
		      !strcmp(at_s(r, "messages.2.content.0.image.source.bytes"), "iVBORw0KGgoAAAANSUhEUg=="), "bedrock image");
		CHECK(!strcmp(at_s(r, "toolConfig.toolChoice.tool.name"), "scribe_reply") &&
		      at(at(r, "toolConfig.tools.0.toolSpec.inputSchema.json.properties"), schema_prop), "bedrock nova forced tool");
		CHECK(strlen(at_s(r, "system.0.text")) > 100 && !at(r, "model") && !at(r, "outputConfig"), "bedrock system");
		/* Claude on Bedrock: native JSON-schema output instead of a tool */
		snprintf(j->model, sizeof j->model, "global.anthropic.claude-haiku-5-5");
		free(req);
		cJSON_Delete(r);
		req = build_request(j, "iVBORw0KGgoAAAANSUhEUg==");
		r = cJSON_Parse(req);
		snprintf(path, sizeof path, "%s/request-bedrock-claude-%s.json", out_dir, mode);
		write_text(path, req);
		{
			cJSON *sch = cJSON_Parse(at_s(r, "outputConfig.textFormat.structure.jsonSchema.schema"));
			CHECK(!strcmp(at_s(r, "outputConfig.textFormat.type"), "json_schema") && at(at(sch, "properties"), schema_prop) &&
			      !at(r, "toolConfig"), "bedrock claude textFormat");
			cJSON_Delete(sch);
		}
		break;
	case API_CHAT: {
		char want[128];
		snprintf(want, sizeof want, "%s/chat/completions", p->base_url);
		CHECK(!strcmp(url, want), "%s url %s", p->id, url);
		CHECK(strstr(hdr, "Authorization: Bearer test-key-") != NULL, "%s header", p->id);
		CHECK(!strcmp(at_s(r, "messages.0.role"), "system") && !strcmp(at_s(r, "messages.2.role"), "assistant"), "%s roles", p->id);
		const char *img = prov == PROV_MISTRAL ? at_s(r, "messages.3.content.1.image_url") : at_s(r, "messages.3.content.1.image_url.url");
		CHECK(!strncmp(img, "data:image/png;base64,iVBOR", 27), "%s image", p->id);
		CHECK(!strcmp(at_s(r, "response_format.type"), "json_schema") &&
		      at(at(r, "response_format.json_schema.schema.properties"), schema_prop), "%s schema", p->id);
		CHECK((prov == PROV_OPENROUTER) == cJSON_IsTrue(at(r, "provider.require_parameters")), "%s require_parameters", p->id);
		if (prov == PROV_OPENROUTER) { /* a model without response_format: the schema goes into the prompt */
			snprintf(j->model, sizeof j->model, "amazon/nova-2-lite-v1");
			free(req);
			cJSON_Delete(r);
			req = build_request(j, "iVBORw0KGgoAAAANSUhEUg==");
			r = cJSON_Parse(req);
			CHECK(!at(r, "response_format") && !at(r, "provider") && strstr(at_s(r, "messages.0.content"), "JSON Schema: {"),
			      "openrouter prompt-only schema");
		}
		break;
	}
	}
	cJSON_Delete(r);
	free(req);
	brain_job_free(j);
}

static void check_key_check(const char *prov, int st, const char *body, int want_ok, const char *want_url) {
	KeyCheck *k = brain_key_check_prepare(&app, prov, "test-key-0123456789abcdef");
	CHECK(k != NULL, "%s: key check job", prov);
	if (!k) return;
	CHECK(!strcmp(k->url, want_url), "%s: key check url %s", prov, k->url);
	char msg[200];
	int ok = brain_key_check_result(k, st, body, strlen(body), msg, sizeof msg);
	CHECK(ok == want_ok, "%s: key check %d -> %d (%s)", prov, st, ok, msg);
	brain_key_check_free(k);
}

int main(int argc, char **argv) {
	out_dir = argc > 1 ? argv[1] : "/tmp";
	/* ---- config: old keys.txt carries over, drop files and pasted keys go into the store ---- */
	mkdir(SECURE_DIR, 0700);
	mkdir(USB_DIR, 0755);
	write_text(SECURE_DIR "/keys.txt", "anthropic_api_key=test-anthropic-key-0123456789\nopenai_api_key=\nresend_api_key=test-resend-0123456789\n");
	write_text(USB_DIR "/gemini-key.txt", "  test-gemini-key-0123456789abcdef\n");
	write_text(USB_DIR "/config.txt", "provider = auto\nmistral_api_key = test-mistral-key-0123456789\nbedrock_region = eu-west-1\n"
					  "bedrock_region = bad region!\nxai_model = grok-4.7\ngemini_model = bad model\"\nnotebook = t\n");
	brain_load_config(&app);
	CHECK(!strcmp(key_get("anthropic_api_key"), "test-anthropic-key-0123456789"), "old Claude key kept");
	CHECK(!strcmp(key_get("resend_api_key"), "test-resend-0123456789") && !strcmp(app.resend_key, "test-resend-0123456789"), "resend key kept");
	CHECK(!strcmp(key_get("gemini_api_key"), "test-gemini-key-0123456789abcdef"), "gemini key from the drop file");
	CHECK(access(USB_DIR "/gemini-key.txt", F_OK) != 0, "drop file deleted");
	CHECK(!strcmp(key_get("mistral_api_key"), "test-mistral-key-0123456789"), "pasted mistral key");
	CHECK(file_has(USB_DIR "/config.txt", "mistral_api_key = saved-on-device") && !file_has(USB_DIR "/config.txt", "test-mistral"),
	      "pasted key blanked in config.txt");
	CHECK(file_has(SECURE_DIR "/keys.txt", "mistral_api_key=test-mistral-key-0123456789"), "keys.txt has the mistral key");
	CHECK(!strcmp(app.bedrock_region, "eu-west-1"), "region %s (a bad one is ignored)", app.bedrock_region);
	CHECK(!strcmp(brain_model(&app, provider_find("xai")), "grok-4.7"), "xai model");
	CHECK(!strcmp(brain_model(&app, provider_find("gemini")), "gemini-3.8-flash"), "bad gemini model ignored");
	CHECK(app.provider == PROV_ANTHROPIC, "auto picks Claude first (%d)", app.provider);
	brain_config_apply(&app, "provider", "gemini");
	CHECK(app.provider == PROV_GEMINI, "provider = gemini");
	brain_config_apply(&app, "provider", "bedrock");
	CHECK(app.provider == PROV_ANTHROPIC, "bedrock without a key falls back to auto");
	key_set("bedrock_api_key", "ABSKtestbedrockkey0123456789");
	brain_keys_changed(&app);
	CHECK(app.provider == PROV_BEDROCK, "bedrock after its key is added");
	CHECK(strstr(brain_provider_name(&app), "Bedrock global.amazon.nova-2-lite-v1:0 (eu-west-1)") != NULL, "name %s", brain_provider_name(&app));
	key_set("bedrock_api_key", "");
	key_set("anthropic_api_key", "");
	brain_keys_changed(&app);
	CHECK(app.provider == PROV_GEMINI, "auto after keys removed: %d", app.provider);
	key_set("gemini_api_key", "");
	key_set("mistral_api_key", "");
	brain_keys_changed(&app);
	CHECK(app.provider == PROV_NONE, "no keys -> none");
	/* a new install: config.txt lists every provider */
	unlink(USB_DIR "/config.txt");
	cJSON_Delete(hist);
	brain_load_config(&app);
	for (int i = 0; i < providers_count(); i++) {
		const Provider *p = provider_at(i);
		char l1[96], l2[128];
		snprintf(l1, sizeof l1, "\n%s =\n", p->key_name);
		snprintf(l2, sizeof l2, "\n%s = %s\n", p->model_key, p->default_model);
		CHECK(file_has(USB_DIR "/config.txt", l1) && file_has(USB_DIR "/config.txt", l2), "new config has %s", p->id);
		CHECK(provider_model_label(p, p->default_model) != NULL, "%s default model is in its list", p->id);
		CHECK(provider_find(p->id) == p && provider_by_prov(p->prov) == p, "%s lookup", p->id);
	}
	CHECK(file_has(USB_DIR "/config.txt", "\nbedrock_region = us-east-1\n") && file_has(USB_DIR "/config.txt", "email_to ="), "new config region + rest");

	/* ---- hardening: many short pasted-key lines grow when rewritten (heap overflow before), keys.txt is 0600,
	 * a planted symlink in place of a private directory is moved aside ---- */
	{
		size_t cap = 400 * 16 + 1, n = 0;
		char *big = malloc(cap);
		for (int i = 0; i < 400; i++) n += (size_t)snprintf(big + n, cap - n, "xai_api_key=k\n");
		write_text(USB_DIR "/config.txt", big);
		free(big);
		char target[512];
		snprintf(target, sizeof target, "%s/elsewhere", out_dir);
		mkdir(target, 0755);
		rename(SECURE_DIR "/history", SECURE_DIR "/history.real");
		CHECK(symlink(target, SECURE_DIR "/history") == 0, "planted a symlink");
		cJSON_Delete(hist);
		brain_load_config(&app);
		char *c = read_file(USB_DIR "/config.txt", NULL);
		int lines = 0;
		for (char *p = c; p && (p = strstr(p, "xai_api_key = saved-on-device\n")); p++) lines++;
		CHECK(lines == 400, "all 400 key lines rewritten (%d)", lines);
		free(c);
		struct stat st;
		CHECK(stat(SECURE_DIR "/keys.txt", &st) == 0 && (st.st_mode & 0777) == 0600, "keys.txt is 0600 (%o)", st.st_mode & 0777);
		CHECK(lstat(SECURE_DIR "/history", &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700,
		      "history is a private directory again");
		CHECK(lstat(SECURE_DIR, &st) == 0 && (st.st_mode & 0777) == 0700, "SECURE_DIR is 0700 (%o)", st.st_mode & 0777);
		key_set("xai_api_key", "");
		unlink(USB_DIR "/config.txt");
		cJSON_Delete(hist);
		brain_load_config(&app);
	}

	/* ---- requests ---- */
	snprintf(app.bedrock_region, sizeof app.bedrock_region, "eu-west-1");
	for (int i = 0; i < providers_count(); i++) {
		check_request(provider_at(i)->prov, "talk");
		check_request(provider_at(i)->prov, "math");
		check_request(provider_at(i)->prov, "email");
	}

	/* ---- the drawing pass: a text-only request with the strokes schema, the strongest model, the user's on retry ---- */
	for (int i = 0; i < providers_count(); i++) {
		const Provider *p = provider_at(i);
		AiJob *j = make_job(p->prov, "draw");
		snprintf(j->draw, sizeof j->draw, "a cat sitting");
		char *req = build_request(j, NULL);
		cJSON *dr = req ? cJSON_Parse(req) : NULL;
		CHECK(dr != NULL, "%s: draw request is JSON", p->id);
		cJSON_Delete(dr);
		CHECK(req && strstr(req, "Draw: a cat sitting") && strstr(req, "strokes") && !strstr(req, "iVBOR") &&
		      !strstr(req, "image"), "%s: draw request (text only, strokes schema)", p->id);
		CHECK(req && strstr(req, "1000 x 1000"), "%s: illustrator prompt", p->id);
		free(req);
		brain_job_free(j);
	}
	app.provider = PROV_ANTHROPIC;
	{
		AiJob *dj = brain_prepare_draw(&app, "a cat sitting", 0);
		CHECK(dj && !strcmp(dj->model, "claude-opus-5-5") && !dj->draw_own && !strcmp(dj->mode, "draw"), "Claude draws with Opus");
		brain_job_free(dj);
		dj = brain_prepare_draw(&app, "a cat sitting", 1);
		CHECK(dj && !strcmp(dj->model, "claude-sonnet-5-5") && dj->draw_own, "the retry uses the chosen model (fast -> Sonnet)");
		brain_job_free(dj);
		CHECK(brain_prepare_draw(&app, "", 0) == NULL, "nothing to draw -> no job");
	}
	app.provider = PROV_BEDROCK;
	{
		AiJob *dj = brain_prepare_draw(&app, "a cat sitting", 0);
		CHECK(dj && !strcmp(dj->model, "global.amazon.nova-2-lite-v1:0") && dj->draw_own, "Bedrock keeps the chosen model");
		brain_job_free(dj);
	}
	app.provider = PROV_NONE;

	/* ---- responses ---- */
	check_parse("anthropic", PROV_ANTHROPIC, NULL);
	check_parse("openai", PROV_OPENAI, NULL);
	check_parse("gemini", PROV_GEMINI, NULL);
	check_parse("bedrock nova tool", PROV_BEDROCK, NULL);
	check_parse("bedrock claude text", PROV_BEDROCK, "text");
	check_parse("xai", PROV_XAI, NULL);
	check_parse("mistral", PROV_MISTRAL, NULL);
	check_parse("mistral chunks", PROV_MISTRAL, "chunks");
	check_parse("openrouter", PROV_OPENROUTER, NULL);
	check_parse("openrouter fenced", PROV_OPENROUTER, "fenced");
	check_refusal("anthropic", PROV_ANTHROPIC, "refusal");
	check_refusal("gemini", PROV_GEMINI, "blocked");
	check_refusal("bedrock", PROV_BEDROCK, "guardrail");
	check_refusal("openrouter", PROV_OPENROUTER, "refusal");

	/* ---- errors (bodies as the services return them for a bad key) ---- */
	check_error("anthropic 401", PROV_ANTHROPIC, 401, "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"}}", "Claude rejected the API key");
	check_error("gemini 400", PROV_GEMINI, 400, "{\"error\":{\"code\":400,\"message\":\"API key not valid. Please pass a valid API key.\",\"status\":\"INVALID_ARGUMENT\"}}", "Gemini rejected the API key");
	check_error("xai 400", PROV_XAI, 400, "{\"code\":\"invalid-argument\",\"error\":\"Incorrect API key provided. You can obtain an API key from https://console.x.ai.\"}", "Grok rejected the API key");
	check_error("mistral 401", PROV_MISTRAL, 401, "{\"detail\":\"Invalid API Key\"}", "Mistral rejected the API key");
	check_error("openrouter 401", PROV_OPENROUTER, 401, "{\"error\":{\"message\":\"Missing Authentication header\",\"code\":401}}", "OpenRouter rejected the API key");
	check_error("bedrock 403 key", PROV_BEDROCK, 403, "{\"Message\":\"Invalid API Key format: Must start with pre-defined prefix\"}", "Bedrock rejected the API key");
	check_error("bedrock 403 model", PROV_BEDROCK, 403, "{\"message\":\"You don't have access to the model with the specified model ID.\"}", "Bedrock error 403: You don't have access");
	check_error("bedrock 400", PROV_BEDROCK, 400, "{\"message\":\"The provided model identifier is invalid.\"}", "Bedrock error 400: The provided model identifier is invalid.");
	check_error("mistral 422", PROV_MISTRAL, 422, "{\"object\":\"error\",\"message\":{\"detail\":[]},\"type\":\"invalid_request_error\",\"detail\":[{\"msg\":\"Input should be a valid string\"}]}", "Mistral error 422: Input should be a valid string");
	check_error("gemini 429", PROV_GEMINI, 429, "{\"error\":{\"code\":429,\"message\":\"Resource has been exhausted\",\"status\":\"RESOURCE_EXHAUSTED\"}}", "busy or at a usage limit");
	check_error("no network", PROV_XAI, -1006, "", "no internet connection");
	check_error("xai 400 tokens", PROV_XAI, 400, "{\"code\":\"invalid-argument\",\"error\":\"max_tokens must be at most 64000 tokens\"}", "Grok error 400: max_tokens");

	/* ---- Test key ---- */
	snprintf(app.bedrock_region, sizeof app.bedrock_region, "ca-central-1");
	check_key_check("anthropic", 200, "{\"data\":[],\"has_more\":true}", 1, "https://api.anthropic.com/v1/models?limit=1");
	check_key_check("openai", 401, "{\"error\":{\"message\":\"Incorrect API key provided\"}}", 0, "https://api.openai.com/v1/models");
	check_key_check("gemini", 400, "{\"error\":{\"code\":400,\"message\":\"API key not valid. Please pass a valid API key.\"}}", 0,
			"https://generativelanguage.googleapis.com/v1beta/models?pageSize=1");
	check_key_check("bedrock", 200, "{\"modelSummaries\":[]}", 1, "https://bedrock.ca-central-1.amazonaws.com/foundation-models?byProvider=amazon");
	check_key_check("bedrock", 403, "{\"Message\":\"User: arn:aws:iam::123456789012:user/BedrockAPIKey-x is not authorized to perform: bedrock:ListFoundationModels\"}",
			1, "https://bedrock.ca-central-1.amazonaws.com/foundation-models?byProvider=amazon");
	check_key_check("bedrock", 403, "{\"Message\":\"Invalid API Key format: Delimiter ':' not found\"}", 0,
			"https://bedrock.ca-central-1.amazonaws.com/foundation-models?byProvider=amazon");
	check_key_check("xai", 400, "{\"code\":\"invalid-argument\",\"error\":\"Incorrect API key provided.\"}", 0, "https://api.x.ai/v1/models");
	check_key_check("mistral", 200, "{\"object\":\"list\",\"data\":[]}", 1, "https://api.mistral.ai/v1/models");
	check_key_check("openrouter", 200, "{\"data\":{\"label\":\"sk-or-v1-abc...123\",\"limit\":null,\"usage\":0}}", 1, "https://openrouter.ai/api/v1/key");
	check_key_check("openrouter", -1006, "", 0, "https://openrouter.ai/api/v1/key");
	CHECK(brain_key_check_prepare(&app, "nope", "k") == NULL && brain_key_check_prepare(&app, "gemini", "") == NULL, "key check without key");

	/* ---- the fake_response.json developer hook still replaces the API call, for every provider ---- */
	mkdir(RUN_DIR, 0700);
	for (int i = 0; i < providers_count(); i++) {
		const Provider *p = provider_at(i);
		char *canned = response_for(p->api, NULL, INNER);
		write_text(SECURE_DIR "/fake_response.json", canned);
		AiJob *j = make_job(p->prov, "talk");
		char *body = NULL;
		size_t len = 0;
		int st = brain_run(j, &body, &len);
		CHECK(st == 200 && body && !strcmp(body, canned), "%s: fake response returned", p->id);
		CHECK(file_has(RUN_DIR "/last_req.json", "\"scribe_reply\"") || p->api == API_ANTHROPIC || p->api == API_GEMINI,
		      "%s: request kept for inspection", p->id);
		CHECK(access(RUN_DIR "/hdr.txt", F_OK) != 0, "%s: no header file (with the key) left behind", p->id);
		Reply r = { 0 };
		CHECK(brain_parse(&app, j, st, body, len, &r) && !strcmp(r.reply, "Check the sum: 2 + 2 = 4."), "%s: fake response parsed", p->id);
		reply_clear(&r);
		free(body);
		free(canned);
		brain_job_free(j);
	}
	unlink(SECURE_DIR "/fake_response.json");
	cJSON_Delete(hist);

	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
