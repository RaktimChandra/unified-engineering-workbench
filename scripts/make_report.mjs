// Documentation tooling only: inlines the committed SVG figures into
// docs/report/report.html and prints it to PDF with headless Chromium.
//   node scripts/make_report.mjs   ->  docs/report/Report.pdf
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
let html = readFileSync(path.join(root, 'docs/report/report.html'), 'utf8');
html = html.replace(/\{\{SVG:([^}]+)\}\}/g, (_, rel) =>
  readFileSync(path.join(root, rel), 'utf8').replace(/<\?xml[^>]*>/, ''));
const tmp = path.join(root, 'docs/report/.report.inlined.html');
writeFileSync(tmp, html);
const browser = await chromium.launch();
const page = await browser.newPage();
await page.goto('file://' + tmp);
await page.pdf({
  path: path.join(root, 'docs/report/Report.pdf'), format: 'A4', printBackground: true,
  displayHeaderFooter: true, headerTemplate: '<span></span>',
  footerTemplate: '<div style="font-size:7px;width:100%;text-align:center;color:#888">Unified Engineering Workbench - RA2311033010038 - page <span class="pageNumber"></span> / <span class="totalPages"></span></div>',
  margin: { top: '12mm', bottom: '13mm', left: '13mm', right: '13mm' },
});
await browser.close();
console.log('wrote docs/report/Report.pdf');
