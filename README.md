# Inkling

<img src="images/icon.png" alt="Inkling icon" width="150" align="right">

**A notebook that writes back, on a jailbroken Kindle Scribe.** Write with the pen and an AI answers on the page, in
handwriting, stroke by stroke. Ask it anything, have your notes typed up neatly, get your maths checked, translate,
or play one of 37 games.

Inkling runs entirely on the Kindle and talks directly to the AI service you choose (**Claude**, **OpenAI**,
**Google Gemini**, **Amazon Bedrock**, **xAI Grok**, **Mistral** or **OpenRouter**) with **your own API key**. No
computer or server is needed once it's installed.

**[▶ Watch the demo (88 seconds)](../../releases/download/v0.3.0/inkling-demo.mp4)**

Free for personal use. Published by JSSL. This repository holds the downloads and documentation only; the source code
isn't public.

<p>
<img src="images/chat.png" alt="Chat: the handwritten question 'Hi! Any tips for a rainy Sunday?' and the AI's handwritten reply underneath" width="200">
<img src="images/type-up.png" alt="Type up: a handwritten shopping list (eggs, milk, bread) turned into neat typed text" width="200">
<img src="images/math.png" alt="Math: the handwritten sum '12x4 = 45' checked step by step, showing the answer is 48" width="200">
<img src="images/translate.png" alt="Translate: 'Good morning, how are you?' written back in Spanish as 'Buenos días, ¿cómo estás?'" width="200">
</p>
<p>
<img src="images/drawing.png" alt="Asked 'Can you draw me a cat?', the AI wrote 'Here's a cat for you!' and drew a sitting cat below" width="200">
<img src="images/games.png" alt="The games menu: 21 board and puzzle games and 16 AI games, with Easy, Medium and Hard" width="200">
<img src="images/mode-picker.png" alt="The Mode drop-down open: Chat, Type up, Math, Translate and Games" width="200">
<img src="images/handwriting-styles.png" alt="Settings, Handwriting & fonts: the 30 handwriting styles, size, writing speed and typed fonts" width="200">
</p>

## Contents

- [What it does](#what-it-does)
- [What you need](#what-you-need)
- [Download and install](#download-and-install)
- [Add your API key](#add-your-api-key)
- [AI services and costs](#ai-services-and-costs)
- [Using Inkling](#using-inkling)
- [Email a page to yourself](#email-a-page-to-yourself)
- [Updating](#updating)
- [Uninstalling](#uninstalling)
- [Troubleshooting](#troubleshooting)
- [Privacy](#privacy)
- [Reporting problems](#reporting-problems)
- [Licence](#licence)

## What it does

- **Chat**: write anything; the AI writes back under your writing in handwriting, like a pen pal or a diary that
  answers. 18 everyday voices (friendly, mentor, poetic, Shakespeare...) and 12 fantasy voices (evil wizard, dragon,
  fairy...).
- **Type up**: your handwriting turns into neat typed text in place, with headings, lists and tables.
- **Math**: write a problem or your working; it checks it step by step (hints first, or full solutions).
- **Translate**: write in any language and it writes it back in the language you choose, in handwriting.
- **It can draw**: ask it to draw something and, a few seconds after its reply, a detailed line drawing appears
  stroke by stroke, made by your AI service's most capable model.
- **Pages**: flick left or right with a finger to turn pages; the conversation carries on across pages.
- **Games**: 21 board, puzzle and word games the Kindle plays itself (tic-tac-toe, connect four, reversi, sudoku,
  minesweeper, word search, nonograms, crosswords and more, with Easy/Medium/Hard), plus 16 AI games (I draw you
  guess, 20 questions, trivia, mystery detective, dungeon quest...).
- **30 handwriting styles**, from looped cursive to blackletter, and the Kindle's own fonts for typed text.
- **Your notebook stays on the Kindle**: each page as an image, and the whole conversation as a text file you can
  copy to your computer.

## What you need

1. **A Kindle Scribe** (built and tested on the first-generation Scribe, firmware 5.19.6).
2. **A jailbroken Kindle** with the community jailbreak's *shell integration* (it lets you open `.sh` files from your
   Library) and root access. See [kindlemodding.org](https://kindlemodding.org); jailbreaking isn't covered here.
   Check that you can see and open `.sh` files in your Library before installing Inkling.
3. **Wi-Fi** on the Kindle.
4. **An API key** from one of the [AI services](#ai-services-and-costs), with billing set up on that account.
5. **A computer** and the Kindle's USB cable, for the install.

## Download and install

1. **Download** `inkling-<version>.zip` from the [latest release](../../releases/latest) and unzip it on your computer.
   Inside you'll find `README.txt`, `LICENSE.md`, a `documents` folder with `Install Inkling.sh`, and an `inkling`
   folder (the app and its licences).
2. **Connect the Kindle** to your computer with the USB cable. It shows up as a drive with a `documents` folder.
3. **Copy the `inkling` folder** from the zip to the Kindle's main folder, next to `documents`.
4. **Copy `Install Inkling.sh`** from the zip's `documents` folder into the Kindle's `documents` folder.
5. **Eject** the Kindle (use your computer's eject or safely remove), then unplug it.
6. On the Kindle, open your **Library**, find **Install Inkling** and tap it. After a few seconds a message says
   *"Inkling ... installed"*.
7. An **Inkling** item with a quill icon is now in your Library. Next, add your API key.

The installer copies the app into the Kindle's protected storage and adds the Inkling item to your Library. It
doesn't change anything else on the Kindle.

## Add your API key

1. On your computer, make a plain text file that contains only your key, named for the service:

   | Service | Key file |
   |---|---|
   | Claude (Anthropic) | `anthropic-key.txt` |
   | OpenAI | `openai-key.txt` |
   | Google Gemini | `gemini-key.txt` |
   | Amazon Bedrock | `bedrock-key.txt` |
   | xAI Grok | `xai-key.txt` |
   | Mistral | `mistral-key.txt` |
   | OpenRouter | `openrouter-key.txt` |

   Use a plain-text editor and save as plain text (not a formatted document).
2. Connect the Kindle and copy the file into the **`inkling`** folder on the Kindle. Eject.
3. Open **Inkling**. It moves the key into protected storage on the Kindle and **deletes the text file**. The status
   line at the top says which AI it's using.

You can also type or paste a key on the Kindle in **Menu > Settings > AI & keys**, where **Test key** checks it
without spending anything. Keys for several services can be stored; pick the one to use and its model on the same
page.

## AI services and costs

Every service bills your own account; nothing goes through any other server. A reply sends a picture of the page, the
instructions and the recent conversation (a few thousand tokens) and gets a few hundred tokens back.

| Service | Where to get a key | Default model | Cost of a reply |
|---|---|---|---|
| Claude | [console.anthropic.com](https://console.anthropic.com) | `fast` (Haiku, with Sonnet for maths) | under a cent with the default; about 1-2 cents with Opus |
| OpenAI | [platform.openai.com](https://platform.openai.com) | `gpt-6.1-sol` | depends on the model |
| Google Gemini | [aistudio.google.com](https://aistudio.google.com) > Get API key | `gemini-3.8-flash` | a fraction of a cent; the free tier costs nothing, but Google uses free-tier prompts to improve its products |
| Amazon Bedrock | AWS console > Amazon Bedrock > API keys (use a long-term key) | `global.amazon.nova-2-lite-v1:0` | billed to your AWS account |
| xAI Grok | [console.x.ai](https://console.x.ai) | `grok-4.3` | about a cent |
| Mistral | [console.mistral.ai](https://console.mistral.ai) | `mistral-large-4` | under a cent |
| OpenRouter | [openrouter.ai/keys](https://openrouter.ai/keys) | `google/gemini-3.8-flash` | each model's own price, from prepaid credits |

**Amazon Bedrock** also needs the AWS region your key works in (Settings > AI & keys, or `bedrock_region` in
`inkling/config.txt`; default `us-east-1`).

A drawing is made by the service's most capable model and costs a little more than a reply (a few cents with
Claude). The board and puzzle games run on the Kindle and cost nothing; AI games cost about the same as chatting. **Set a
monthly spending limit** on your key in the service's dashboard.

## Using Inkling

Open **Inkling** from your Library. The top bar has:

- **Mode** (shows the current one, e.g. **Chat ▾**): Chat, Type up, Math, Translate or Games.
- **Clear**: clear this page, forget the conversation, delete this page, or start over. Tap an option twice: the first
  tap turns it dark.
- **Send**: answer right away instead of waiting for your pause, or **Email this page**.
- **Menu**: new page, **Settings**, **Check for updates**, **Exit**.

**Writing.** Write with the pen, then lift it and pause (1.5 seconds by default; Settings > Pen & timing). The reply
appears under your writing a couple of seconds later. If you carry on writing before it answers, it waits for your
next pause.

**Fixing mistakes.** Rub out with the eraser end of the pen if yours has one, or scribble back and forth over a word
to scratch it out. Rubbing out doesn't make the AI answer by itself; write the correction and it answers as usual.

**Pages.** Flick left with a finger for the next page (from the last page, that starts a new one); flick right for
the previous page.

**Leaving.** **Menu > Exit**, or press the **power button** (Inkling closes so the Kindle can sleep and lock as usual).
Your page is saved either way, and Inkling opens on the same page next time.

**Settings** (Menu > Settings): voices, reply length and language, handwriting style and size, typed font, translation
language, maths help, pen and pause timing, AI service and model, and email. The same settings are saved in
`inkling/config.txt`, which you can also edit on a computer. Your notebook is in `inkling/notebooks/` on the Kindle's
USB storage: the pages as `page-0001.png` and so on, and the conversation as a `.md` text file.

## Email a page to yourself

**Send > Email this page** turns the page into an email: the AI types it up (a shopping list as a list, maths with its
working) and the page itself is attached as a picture. It goes out through your own email account, to yourself or to
the address you set.

Set it up once in **Menu > Settings > Email**: pick your email service, type your address and an **app password**. An
app password is a separate password your email service makes for one app; Inkling never needs your real password, and
you can delete the app password at any time. It stays on the Kindle in protected storage.

- **Gmail**: turn on 2-Step Verification, then go to myaccount.google.com/apppasswords, name it "Inkling" and type the
  16 letters it shows. Work or school accounts have this only if their administrator allows it.
- **Outlook, Hotmail, Live**: account.microsoft.com > Security > Advanced security options: turn on two-step
  verification, then App passwords > Create a new app password.
- **iCloud Mail**: account.apple.com > Sign-In and Security > App-Specific Passwords. Use your @icloud.com address.
- **Yahoo Mail**: Account Info > Account Security > Generate app password.
- **Fastmail**: Settings > Privacy & Security > Manage app passwords and access > New app password, with SMTP access.
- **Other server**: your mail service's outgoing (SMTP) server and port (465 for SSL/TLS, 587 for STARTTLS). The
  connection is always encrypted and the server's certificate is checked.
- **Resend** ([resend.com](https://resend.com), a sending service with a free tier) instead of your own account: sign
  up with the address you want pages sent to, create an API key with sending access, and enter it as the Resend key
  (or copy a `resend-key.txt` file holding only the key into the `inkling` folder).

If sending fails, the status line says why: *"refused the password"* means the app password is wrong or was deleted,
*"no connection"* means the Kindle couldn't reach the mail server, and a *certificate* message usually means the
Kindle's clock is wrong.

## Updating

**Menu > Check for updates.** The first tap asks GitHub whether a newer version is out; the second tap downloads it,
checks it, installs it and restarts Inkling. Your settings, keys and notebooks are kept, the previous version comes
back by itself if the new one fails to start, and nothing is checked or downloaded unless you tap.

Every update is signed with JSSL's release key, and Inkling carries the matching public key: it installs an update
only when the signature is valid and the download matches the signed checksum.

**Over USB** (always works): download the new release, copy its `inkling` folder and `Install Inkling.sh` to the
Kindle as in [Download and install](#download-and-install) (replacing the old ones), eject, and tap **Install
Inkling** again.

## Uninstalling

Copy **`Uninstall Inkling.sh`** from `inkling/install/` on the Kindle into the `documents` folder, eject, and tap
**Uninstall Inkling** in your Library. It removes the app, its Library item, your stored keys and the AI's
conversation memory. Your notebooks and `config.txt` stay in the `inkling` folder until you delete it yourself.

## Troubleshooting

- **"Install Inkling" or "Inkling" doesn't appear in the Library**: make sure the `.sh` file is directly inside
  `documents`, eject properly, and give the Library a minute to refresh. If `.sh` files never appear, the jailbreak's
  shell integration isn't installed.
- **Nothing happens when tapping Install Inkling**: look at `inkling/install-log.txt` on the Kindle. *"root (su) not
  found"* means the Kindle isn't fully jailbroken.
- **"Add your API key..."**: no key yet; see [Add your API key](#add-your-api-key).
- **"... rejected the API key"**: the key is wrong or revoked; make a new one.
- **"no internet connection?"**: check the Kindle's Wi-Fi, then tap **Send**.
- **"... is busy or at a usage limit"**: the AI service is overloaded or your spending limit was reached.
- **The screen looks stuck**: press the power button; Inkling closes and the Kindle returns to normal.

## Privacy

- **Your keys and app password stay on the Kindle**, in a folder only the system can read (not on the USB storage),
  and each is sent only to its own service. If the Kindle is lost, revoke the keys in the services' dashboards.
- **Your pages go only to the AI service you chose**: when it answers, Inkling sends a picture of the current page and
  the recent conversation. That service's privacy terms apply. Emails go through the email service you set up.
- **Nothing else leaves the Kindle.** There are no accounts, analytics or tracking; JSSL never receives your pages,
  keys or replies. Inkling contacts GitHub only when you tap Check for updates.

## Reporting problems

Bug reports are welcome: open an issue in this repository's [Issues](../../issues) tab and fill in the bug report
form. **Never paste API keys, app passwords or tokens** into an issue. This repository doesn't accept pull requests or
other changes, and the source code isn't public.

## Licence

Inkling is © 2026 JSSL. It's free for personal, non-commercial use under the [Inkling End-User Licence](LICENSE.md):
you may install and use it on your own Kindle, but you may not redistribute, sell, modify or reverse engineer it. It
comes with no warranty, and you're responsible for your own AI and email accounts and their costs. Third-party
components and their licences are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Kindle and Kindle Scribe are trademarks of Amazon. Inkling isn't made or endorsed by Amazon or by any AI service.
