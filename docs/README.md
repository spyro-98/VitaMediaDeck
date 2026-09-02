# VitaMediaDeck Documentation

This directory contains the Fumapress (Fumadocs) documentation website for
VitaMediaDeck. It is a self-contained Node.js sub-project and does not
participate in the VitaSDK/CMake build.

## Development

```sh
npm install
npm run dev
```

The dev server runs locally and watches `content/` for changes.

## Build

```sh
npm run build
```

Static output is written to `dist/public`, which the GitHub Actions workflow
deploys to GitHub Pages at `/VitaMediaDeck/`.

## Structure

- `content/` — Markdown/MDX documentation pages.
- `press.config.tsx` — site configuration, theme, fonts, and layout props.
- `src/app.css` — Tailwind and Fumadocs UI theme imports.