Inkling - write on your Kindle Scribe and an AI writes back on the page
======================================================================

You need: a jailbroken Kindle Scribe (with the standard jailbreak's shell integration), Wi-Fi, and your own API
key from one of the AI services Inkling works with (Claude, OpenAI, Google Gemini, Amazon Bedrock, xAI Grok,
Mistral or OpenRouter).

1. Connect the Kindle to your computer with USB.
2. Copy the "inkling" folder from this zip to the Kindle's main folder (next to "documents").
3. Copy "Install Inkling.sh" from this zip's "documents" folder into the Kindle's "documents" folder.
4. Eject the Kindle. In your Library, tap "Install Inkling". A message says when it's done (a few seconds).
5. Your API key: on your computer, make a plain text file named anthropic-key.txt (or openai-key.txt,
   gemini-key.txt, bedrock-key.txt, xai-key.txt, mistral-key.txt, openrouter-key.txt) that contains only
   the key, and copy it into the inkling folder on the Kindle. Inkling moves it into
   protected storage the next time it starts and deletes the file.
6. Open "Inkling" from your Library and write. Pause about 1.5 seconds and the answer appears.

Inkling used to be called Scribe. If you had it: the installer (or the new version's first start) moves your
settings, notebooks and keys from the old "scribe-claude" folder into "inkling" and replaces the old Library items.

Full instructions, settings and troubleshooting: see the README on the project page.
To update: Menu > Check for updates in Inkling, or copy the new inkling folder and "Install Inkling.sh" again
and tap it.
To remove: copy "Uninstall Inkling.sh" (in inkling/install) into documents and tap it.
Inkling is free software under the GNU General Public License, version 3 (LICENSE.txt), published by JSSL.
Its source code and build instructions are on the project page. Third-party components and their licences:
inkling/licences (THIRD-PARTY-NOTICES.md lists them all).
