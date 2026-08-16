// @ts-check
import {themes as prismThemes} from 'prism-react-renderer';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import ScalarDocusaurus from '@scalar/docusaurus';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const openapiSpec = fs.readFileSync(
  path.join(__dirname, '..', 'openapi', 'cec-dongle.yaml'),
  'utf-8',
);

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'CEC-Dongle',
  tagline: 'REST-controlled HDMI-CEC bridge for the SMLIGHT SLWF-08',
  favicon: 'img/favicon.ico',

  future: {
    v4: true,
  },

  // GitHub Pages project-page hosting. If you point a custom domain at this
  // site via a CNAME file (static/CNAME), change baseUrl to '/' — project
  // pages otherwise serve from /<projectName>/.
  url: 'https://The1TrueJoe.github.io',
  baseUrl: '/CEC-Dongle/',

  organizationName: 'The1TrueJoe',
  projectName: 'CEC-Dongle',

  onBrokenLinks: 'throw',
  markdown: {
    hooks: {
      onBrokenMarkdownLinks: 'warn',
    },
  },

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          sidebarPath: './sidebars.js',
          routeBasePath: 'docs',
          editUrl: 'https://github.com/The1TrueJoe/CEC-Dongle/tree/main/website/',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      }),
    ],
  ],

  plugins: [
    [
      ScalarDocusaurus,
      /** @type {import('@scalar/docusaurus').ScalarOptions} */
      ({
        id: 'api-reference',
        label: 'API Reference',
        route: '/api-reference',
        configuration: {
          content: openapiSpec,
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      colorMode: {
        respectPrefersColorScheme: true,
      },
      navbar: {
        title: 'CEC-Dongle',
        logo: {
          alt: 'CEC-Dongle Logo',
          src: 'img/logo.svg',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'tutorialSidebar',
            position: 'left',
            label: 'Docs',
          },
          // The Scalar plugin auto-injects its own "API Reference" navbar
          // item (driven by its `label` option below) — adding one here too
          // would duplicate it.
          {
            href: 'https://github.com/The1TrueJoe/CEC-Dongle',
            label: 'GitHub',
            position: 'right',
          },
          {
            href: 'https://github.com/The1TrueJoe/CEC-Dongle/releases',
            label: 'Releases',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: 'Docs',
            items: [
              {label: 'Getting Started', to: '/docs/getting-started'},
              {label: 'API Reference', to: '/api-reference'},
              {label: 'Troubleshooting', to: '/docs/troubleshooting'},
            ],
          },
          {
            title: 'Project',
            items: [
              {label: 'GitHub', href: 'https://github.com/The1TrueJoe/CEC-Dongle'},
              {label: 'Releases', href: 'https://github.com/The1TrueJoe/CEC-Dongle/releases'},
              {label: 'Issues', href: 'https://github.com/The1TrueJoe/CEC-Dongle/issues'},
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} Joseph Telaak. Built with Docusaurus.`,
      },
      prism: {
        theme: prismThemes.github,
        darkTheme: prismThemes.dracula,
      },
    }),
};

export default config;
