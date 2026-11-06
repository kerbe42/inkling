/* email.c: Send > Email this page through the user's own mail account (SMTP with an app password). */
#ifndef SCRIBE_EMAIL_H
#define SCRIBE_EMAIL_H
#include <stddef.h>

typedef struct {
	const char *id;   /* the email_provider config value: gmail, outlook, icloud, yahoo, fastmail */
	const char *name; /* for messages: "Gmail" */
	const char *host;
	int port; /* 465 = implicit TLS (smtps://), anything else = STARTTLS (smtp:// + --ssl-reqd) */
} EmailPreset;

/* The preset for a provider id (case-insensitive; "hotmail"/"live" = outlook), NULL for custom, resend or unknown. */
const EmailPreset *email_preset(const char *id);

/* Where to send: the preset, or for "custom" the given server and port (0 = 587). Fills url ("smtps://host:465" or
 * "smtp://host:587"), *starttls and name ("Gmail", "the mail server"). Returns 0, or -1 with a message in err. */
int email_target(const char *provider, const char *server, int port, char *url, size_t urln, int *starttls, char *name,
		 size_t namen, char *err, size_t errn);

/* Writes the message to path (mode 0600): multipart/mixed with a text/plain UTF-8 body (quoted-printable) and the
 * page as image/png (png64 = the PNG in base64 without line breaks; NULL for none). from is the header From (e.g.
 * "Inkling <me@example.com>"), to the recipient, msgid_domain the part after @ in the Message-ID. 0 on success. */
int email_write_mime(const char *path, const char *from, const char *to, const char *subject, const char *text,
		     const char *png64, const char *png_name, const char *msgid_domain);

typedef struct {
	const char *curl;    /* BIN_DIR "/curl-smtp" */
	const char *cacert;  /* /etc/ssl/certs/ca-certificates.crt */
	const char *cfg;     /* the 0600 curl config file that carries the login (deleted afterwards) */
	const char *url;     /* from email_target() */
	int starttls;
	const char *user, *pass; /* the account (its address) and its app password */
	const char *mail_from, *rcpt;
	const char *eml; /* the message file */
	const char *name; /* "Gmail" */
	int max_time;     /* seconds */
} SmtpSend;

/* Runs curl-smtp. Returns 0 when the server accepted the message, else the curl exit code (127: helper missing)
 * with a short message for the status line in err and curl's own error line in detail (for the log; may be NULL). */
int email_smtp_send(const SmtpSend *s, char *err, size_t errn, char *detail, size_t detailn);

/* The short message for a curl exit code and its stderr text (used by email_smtp_send; exposed for the tests). */
void email_error_message(int code, const char *curl_err, const char *name, int starttls, char *err, size_t errn);

#endif
