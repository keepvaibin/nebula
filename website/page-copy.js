/** Messages embedded from the current page's editable JSON. */
const messages = JSON.parse(document.getElementById('page-messages')?.textContent || '{}');
export function pageMessage(key, values = {}) {
  const text = messages[key];
  if (typeof text !== 'string') throw new Error(`Missing page message: ${key}`);
  return text.replace(/\{(\w+)\}/g, (match, name) =>
    Object.hasOwn(values, name) ? String(values[name]) : match,
  );
}
