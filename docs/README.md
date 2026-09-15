# Phone-side pages

Static configuration pages that some of the watchapps open as their settings
screen on the phone. GitHub Pages serves this directory as the site root, so
`restful-hrv/index.html` is published at
`https://mrmastodon.github.io/Pebble-Watch-apps/restful-hrv/`.

These are plain HTML with no backend and no build step. `.nojekyll` turns off
Jekyll processing, since there is nothing here for it to do and it would only
add ways for the build to fail.

| Page | Used by |
|---|---|
| [`restful-hrv/`](restful-hrv) | [`apps/restful-hrv`](../apps/restful-hrv) — shows the measurement history with a chart and CSV export |

Pages is configured under **Settings → Pages → Source: `main` branch, `/docs`
folder**. Note that it builds from `main`, so a page only goes live once its
branch is merged.
