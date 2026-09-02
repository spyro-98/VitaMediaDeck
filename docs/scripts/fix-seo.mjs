import { readFile, writeFile } from "node:fs/promises";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const origin = "https://<your-username>.github.io";
const sub = "/VitaMediaDeck";
const dist = join(root, "dist", "public");

/**
 * @param {string} name
 * @param {(raw: string) => string} transform
 */
async function patch(name, transform) {
  const path = join(dist, name);
  let text = await readFile(path, "utf8");
  const next = transform(text);
  if (next !== text) {
    await writeFile(path, next);
    console.log(`[fix-seo] patched ${name}`);
  } else {
    console.log(`[fix-seo] ${name}: unchanged`);
  }
}

await patch("sitemap.xml", (text) => {
  if (text.includes(`${origin}${sub}/`)) return text;
  return text.split(`${origin}/`).join(`${origin}${sub}/`);
});

await patch("robots.txt", (text) => {
  if (text.includes(`${origin}${sub}/sitemap.xml`)) return text;
  return text
    .split(`Sitemap: ${origin}/sitemap.xml`)
    .join(`Sitemap: ${origin}${sub}/sitemap.xml`);
});

await patch("rss.xml", (text) => {
  return text.split(`href="${origin}/rss.xml"`).join(`href="${origin}${sub}/rss.xml"`);
});
