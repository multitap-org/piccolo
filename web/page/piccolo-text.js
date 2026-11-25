'use strict';

const PICCOLO_WEB_LANGUAGE_KEY = 'piccolo-web-language';
let piccolo_web_language = 'en';

function piccolo_web_text(key, values = {}) {
  const catalogs = PICCOLO_WEB_STRINGS;
  const text = catalogs[piccolo_web_language][key] ?? catalogs.en[key] ?? key;
  return text.replace(/\{([a-z_]+)\}/g, function (placeholder, name) {
    return values[name] ?? placeholder;
  });
}

function piccolo_web_apply_language(code) {
  const language = typeof code === 'string' ? code.toLowerCase().split('-')[0] : '';
  piccolo_web_language = Object.hasOwn(PICCOLO_WEB_STRINGS, language) ? language : 'en';
  document.documentElement.lang = piccolo_web_language;
  for (const element of document.querySelectorAll('[data-i18n]')) {
    element.textContent = piccolo_web_text(element.dataset.i18n);
  }
  for (const element of document.querySelectorAll('[data-i18n-title]')) {
    element.title = piccolo_web_text(element.dataset.i18nTitle);
    if (element.hasAttribute('aria-label')) {
      element.setAttribute('aria-label', element.title);
    }
  }
  if (document.body.hasAttribute('data-drop-hint')) {
    document.body.dataset.dropHint = piccolo_web_text('web_drop_hint');
  }
}

function piccolo_web_set_language(code) {
  piccolo_web_apply_language(code);
  try {
    localStorage.setItem(PICCOLO_WEB_LANGUAGE_KEY, piccolo_web_language);
  } catch (error) {
    // The current page can still use the selected language.
  }
  window.dispatchEvent(new Event('piccolo-language-change'));
}

let piccolo_web_initial_language = navigator.language;
try {
  piccolo_web_initial_language = localStorage.getItem(PICCOLO_WEB_LANGUAGE_KEY) || piccolo_web_initial_language;
} catch (error) {
  // Use the browser's language on the first visit or when storage is unavailable.
}
piccolo_web_apply_language(piccolo_web_initial_language);
