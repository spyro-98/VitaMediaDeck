import { defineConfig } from "fumapress";
import { fumadocsMdx } from "fumapress/adapters/mdx";
import { metaSchema, pageSchema } from "fumapress/adapters/mdx/schema";
import { lucideIconsPlugin } from "fumadocs-core/source/plugins/lucide-icons";
import { defineDocs } from "fumadocs-mdx/macro";
import { createRootLayout } from "fumapress/layouts/root";
import { createDocsLayoutPage } from "fumapress/layouts/docs";

const docsPath = "/VitaMediaDeck";

const DocsLayout = createDocsLayoutPage({
  async render(page) {
    return {
      pageProps: {
        tableOfContent: {
          style: "clerk" as const,
        },
      },
    };
  },
});

const docs = defineDocs({
  dir: "content",
  docs: {
    async: true,
    schema: pageSchema,
    lastModified: true,
    postprocess: {
      includeProcessedMarkdown: true,
    },
  },
  meta: {
    schema: metaSchema,
  },
});

export default defineConfig({
  mode: "static",
  content: docs.toFumadocsSource(),
  site: {
    name: "VitaMediaDeck",
    baseUrl: `https://spyro-98.github.io${docsPath}`,
    git: {
      user: "<your-username>",
      repo: "VitaMediaDeck",
      branch: "master",
      rootDir: "docs",
    },
  },
  loaderOptions: {
    plugins: [lucideIconsPlugin()],
  },
  defaultLayoutProps: {
    nav: {
      title: "VitaMediaDeck",
    },
    // links: [
    //   {
    //     text: "GitHub",
    //     url: "https://github.com/<your-username>/VitaMediaDeck",
    //     external: true,
    //   },
    //   {
    //     text: "Upstream",
    //     url: "https://github.com/spyro-98/VitaMediaDeck",
    //     external: true,
    //   },
    // ],
  },
  renderPage: (props) => <DocsLayout {...props} />,
  meta: {
    root() {
      return (
        <>
          <link rel="icon" href="/VitaMediaDeck/favicon.ico" sizes="32x32" />
          <link rel="icon" href="/VitaMediaDeck/favicon-32x32.png" type="image/png" sizes="32x32" />
          <link rel="icon" href="/VitaMediaDeck/favicon-16x16.png" type="image/png" sizes="16x16" />
          <link rel="apple-touch-icon" sizes="180x180" href="/VitaMediaDeck/apple-touch-icon.png" />
          <link rel="preconnect" href="https://fonts.googleapis.com" />
          <link rel="preconnect" href="https://fonts.gstatic.com" crossOrigin="" />
          <link
            href="https://fonts.googleapis.com/css2?family=Inter:ital,wght@0,100..900;1,100..900&family=JetBrains+Mono:ital,wght@0,100..800;1,100..800&display=swap"
            rel="stylesheet"
          />
        </>
      );
    },
  },
  renderRoot: createRootLayout({
    providerProps: {
      theme: {
        defaultTheme: "dark",
      },
    },
  }),
})
  .adapters(fumadocsMdx());
