![Wine4Office banner](banner.png)

# Wine4Office 🍷📎

**Run modern Microsoft Office on Linux without a Windows VM.** 


> **AI disclosure:** Every Wine4Office investigation, code change, test, and
> document was produced with AI, primarily **ChatGPT-5.6-sol** and
> **Grok 4.5**. I directed and tested the work, but **I do not know C** and
> cannot independently audit the C/C++ code.

---

## Current Status

| App | Status | Notes |
|-----|--------|-------|
| **Word** | ✅ **Great** | Create, edit, save, sign in, updates, RTL languages, PDF export—all smooth |
| **Excel** | ✅ **Pretty Good** | Formulas, charts, CSV, print/PDF work. VBA untested |
| **PowerPoint** | ⚠️ **Opens** | Not functionally tested (slides might slide, might not) |
| **Outlook** | ❌ **Nope** | Still in the "we've heard of email" phase |

**Tested on:** 7th & 12th-gen Intel | **License tested:** Microsoft 365 ProPlus Subscription

---

## Quick Start (The Easy Way)

Don't want to wrestle with builds? Use the **Wine4Office Manager**:

```sh
curl -fsSL https://github.com/ttv20/wine4office/releases/latest/download/install.sh | bash
```

It installs `Wine4OfficeManager` in `~/.local/bin`, creates the application-menu
shortcut, and asks whether to launch **Wine4Office Manager** immediately
(default: Yes). The default Wine environment remains `~/.wine4office`.

---

## Build It Yourself

```bash
git clone <repo>
mkdir build && cd build
../wine4office/configure --enable-archs=i386,x86_64
make -j$(nproc)
sudo make install
```

Full dependencies: [WineHQ Build Guide](https://gitlab.winehq.org/wine/wine/-/wikis/Building-Wine)

---

## Support this project ☕

Wine4Office is a solo, spare-time experiment involving a lot of AI tokens,
test installs, and broken prefixes. If it saved you a Windows VM, consider
buying me some tokens:

**[☕ Buy me tokens on Ko-fi](https://ko-fi.com/W1L423LQH7)**

---

## Why Not WineHQ?

WineHQ's Clean Room Guidelines ban LLM-generated code. This whole thing is AI soup, so it can't be upstreamed. It's a fork, not a patchset.

---

*Wine4Office is not affiliated with WineHQ or Microsoft. All trademarks belong to their respective corporate overlords.*
