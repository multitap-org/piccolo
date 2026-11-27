// piccolo's page in the browser (docs/wasm.md): what's only piccolo's. The
// start (threads, its folder on OPFS, the module) is frontends-common's
// web/multitap-page.js and multitap-storage.js; the FILES panel is
// piccolo-files.js. This adds opening games (OPEN GAME, drag and drop).

'use strict';

function piccolo_page_files() {
  const button = document.getElementById('open');
  const input = document.getElementById('file');
  button.hidden = false;
  button.addEventListener('click', function () { input.click(); });
  input.addEventListener('change', function () {
    if (input.files.length > 0) {
      multitap_page_open(input.files[0]);
    }
    input.value = '';
  });
  // Drops anywhere on the page. Capture phase, so this runs before SDL's own
  // handler (which would only put the file in /tmp/filedrop).
  let depth = 0;
  window.addEventListener('dragenter', function (event) {
    event.preventDefault();
    depth++;
    document.body.classList.add('dragging');
  }, true);
  window.addEventListener('dragleave', function () {
    depth = Math.max(0, depth - 1);
    if (depth === 0) {
      document.body.classList.remove('dragging');
    }
  }, true);
  window.addEventListener('dragover', function (event) { event.preventDefault(); }, true);
  window.addEventListener('drop', function (event) {
    event.preventDefault();
    event.stopPropagation();
    depth = 0;
    document.body.classList.remove('dragging');
    if (event.dataTransfer.files.length === 0) {
      return;
    }
    // On the FILES panel: into the folder it shows. Elsewhere: play it.
    if (document.getElementById('files').contains(event.target)) {
      piccolo_files_upload(Array.from(event.dataTransfer.files));
    } else {
      multitap_page_open(event.dataTransfer.files[0]);
    }
  }, true);
}

// FULL SCREEN: the whole page on the screen, the same layout with the touch
// pad (it steps aside by itself while a gamepad is in use, multitap-pad.js);
// the strip hides while a game is played anyway. The browser's Esc (or
// back) leaves it.
function piccolo_page_fullscreen() {
  const button = document.getElementById('fullscreen');
  const page = document.documentElement;
  if (!page.requestFullscreen) {
    button.hidden = true;
    return;
  }
  button.addEventListener('click', function () {
    if (document.fullscreenElement) {
      document.exitFullscreen();
    } else {
      page.requestFullscreen({ navigationUI: 'hide' }).catch(function (error) { console.log('Full screen: ' + error.message); });
    }
    document.getElementById('canvas').focus();
  });
  document.addEventListener('fullscreenchange', function () {
    button.classList.toggle('active', document.fullscreenElement === page);
  });
}

// The page as before, kept for scripts that call it (tests).
function piccolo_page_open(file) {
  return multitap_page_open(file);
}

function piccolo_page_start() {
  // The dev channel (every push to main) says so, with its commit, and shows
  // piccolo's fps meter (SHOW FPS); a release says nothing.
  const dev = document.body.dataset.channel === 'dev';
  if (dev) {
    document.getElementById('channel').hidden = false;
  }
  piccolo_page_fullscreen();
  multitap_page_start({
    name: 'piccolo',
    title: 'PICCOLO',
    // piccolo's folder is OPFS's root (where its first browser builds kept
    // it); piccoletto's folder there is piccoletto's. cache/: extracted
    // archives, made again when needed.
    opfs_folder: '',
    skipped: ['cache', 'piccoletto'],
    sw: '../sw.js',
    sw_scope: '../',
    // The release's list of every core's piccolo.wasm (web/package.sh): this
    // build's proof that it's official (frontends-common net_build.h).
    build_list: '../wasm.sha256',
    started: function () {
      piccolo_page_files();
      piccolo_files_start();
    },
  });
}

// The touch pad shown or hidden (multitap-pad.js's on_shown): upright, it
// takes the place under the game where FILES was open.
function piccolo_page_pad_shown(shown) {
  const files = document.getElementById('files');
  if (shown && window.matchMedia('(orientation: portrait)').matches && files !== null &&
      !files.classList.contains('closed') && typeof piccolo_files_set_open === 'function') {
    piccolo_files_set_open(false);
  }
}

// ---- The theme: piccolo's own (the one chosen in its settings) on the page ----

const PICCOLO_PAGE_THEME_KEY = 'piccolo-page-theme';

// The page tokens piccolo_page_theme sets (and clears for BLUE): web/site's
// site.css names, from the theme's UI colors.
const PICCOLO_PAGE_THEME_TOKENS = [
  ['--bg', 'field'], ['--band', 'window'], ['--card', 'window'], ['--card-on-band', 'field'], ['--text', 'text'],
  ['--muted', 'text_disabled'], ['--shadow', 'window_dark'], ['--edge', 'window_light'], ['--grid', 'button'],
  ['--accent', 'highlight'], ['--on-accent', 'text_highlight'], ['--heading-accent', 'section'], ['--link', 'section'],
  ['--code-bg', 'window_dark'],
];

// Sets (or, with null, clears) the page's colors. Everything it sets is a
// property on <html>, so clearing is removing them: BLUE is "none set", the
// look the page always had (site.css, light scheme included).
function piccolo_page_theme_set(theme) {
  const root = document.documentElement.style;
  const names = ['color-scheme', '--stage'];
  for (const [name] of PICCOLO_PAGE_THEME_TOKENS) {
    names.push(name);
  }
  for (const property of Array.from(root)) {
    if (property.startsWith('--pad-')) {
      names.push(property);
    }
  }
  for (const name of names) {
    root.removeProperty(name);
  }
  const meta = document.querySelector('meta[name="theme-color"]');
  if (theme === null || typeof theme !== 'object' || typeof theme.ui !== 'object') {
    if (meta !== null) {
      meta.content = '#2A4068';   // site.css's --band
    }
    return;
  }
  for (const [name, key] of PICCOLO_PAGE_THEME_TOKENS) {
    if (typeof theme.ui[key] === 'string') {
      root.setProperty(name, theme.ui[key]);
    }
  }
  root.setProperty('color-scheme', 'dark');
  // The pad: its colors (pad_dpad -> --pad-dpad, pad_a_held -> --pad-a-held...).
  const pad = typeof theme.pad === 'object' && theme.pad !== null ? theme.pad : {};
  for (const key of Object.keys(pad)) {
    root.setProperty('--pad-' + key.replace(/_/g, '-'), pad[key]);
  }
  // Its opacities: the look's [fill, border, labels, background], 0..1.
  if (Array.isArray(theme.look) && theme.look.length === 4) {
    const names = ['--pad-fill', '--pad-border-opacity', '--pad-labels', '--pad-bg-opacity'];
    for (let i = 0; i < 4; i++) {
      root.setProperty(names[i], theme.look[i] * 100 + '%');
    }
    if (typeof pad.background === 'string') {
      // The stage's empty space: the same background and level as the pad's.
      root.setProperty('--stage', 'color-mix(in srgb, ' + pad.background + ' ' + theme.look[3] * 100 + '%, #000)');
    }
  }
  if (meta !== null && typeof theme.ui.bar === 'string') {
    meta.content = theme.ui.bar;
  }
}

// What piccolo (C++) calls when its theme changes, and once at start. `json`
// is a string:
//   null (or "null")  BLUE, the built-in theme: the page's own look
//   { "ui":   { "field", "window", "window_dark", "window_light", "button", "highlight", "text",
//               "text_disabled", "text_highlight", "section", "bar": "#RRGGBB", ... },
//     "pad":  { "background", "dpad", "dpad_held", "a", "a_held", "a_text", "b", ... "x", "y",
//               "shoulder", "stick", "center" (each with _held and _text), "border": "#RRGGBB" },
//     "look": [fill, border, labels, background] }   // 0..1: the theme's pad levels
// Remembered (localStorage) and applied at the next visit's start
// (piccolo_page_theme_apply_stored), before the module is loaded.
function piccolo_page_theme(json) {
  let theme = null;
  try {
    theme = typeof json === 'string' ? JSON.parse(json) : json;
  } catch (error) {
    console.log('piccolo_page_theme: ' + error.message);
    return;
  }
  piccolo_page_theme_set(theme);
  try {
    if (theme === null || theme === undefined) {
      localStorage.removeItem(PICCOLO_PAGE_THEME_KEY);
    } else {
      localStorage.setItem(PICCOLO_PAGE_THEME_KEY, JSON.stringify(theme));
    }
  } catch (error) {
    // Not remembered: the next visit starts in BLUE until piccolo says.
  }
}

function piccolo_page_theme_apply_stored() {
  try {
    const stored = localStorage.getItem(PICCOLO_PAGE_THEME_KEY);
    if (stored !== null) {
      piccolo_page_theme_set(JSON.parse(stored));
    }
  } catch (error) {
    // Nothing stored, or unreadable: BLUE.
  }
}
