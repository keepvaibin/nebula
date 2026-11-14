# Diagnostics drop (Cloudflare Worker)

Stores the sealed reports players choose to send after a benchmark (docs/DIAGNOSTICS_UPLOAD.md). It accepts one
`POST /v1/report`, checks type, size (256 KiB) and magic, rate-limits per address (hashed, never stored) and per day,
writes the body to R2 and answers 204. No route reads, lists or deletes; every other request is 404.

## Once

1. A Cloudflare account; `npm install -g wrangler`; `wrangler login`.
2. Key pair, on your own machine: `python tools/telemetry/diag_reports.py keygen --exe <NebulaNative> --out <offline folder>`.
   Put the printed public key in `runtime/include/galaxy/diag_endpoint.h` (`kPublicKey`). Keep the private key file
   offline and backed up: without it no report can be opened; with it anyone can open them.
3. R2 (needs a payment card on file; nothing is charged inside the free tier):
   `wrangler r2 bucket create nebula-diag-reports`, then
   `wrangler r2 bucket lifecycle add nebula-diag-reports expire-180 --expire-days 180` (or in the dashboard: R2 >
   nebula-diag-reports > Settings > Object lifecycle rules > delete after 180 days).
4. Deploy, from this folder: `wrangler deploy`. Put the printed URL plus `/v1/report`
   (`https://nebula-diag.<account>.workers.dev/v1/report`) in `diag_endpoint.h` (`kEndpoint`) and build the release.

Fetch reports with an R2 API token (Account > R2 > Manage API tokens, Object Read only) and any S3 client, e.g.
`rclone copy r2:nebula-diag-reports ./reports`; then `diag_reports.py open ./reports` and `summary`.

## Cost

Free tier: Workers 100,000 requests a day; R2 10 GB stored, 1 million writes and 10 million reads a month, no egress
fee; Durable Objects (the daily counter) 100,000 requests a day. At ~10-30 KB a report that is 300,000+ reports kept.
Beyond: R2 $0.015 per GB-month, $4.50 per million writes; Workers Paid $5 a month (10 million requests). `DAILY_CAP`
(wrangler.toml, 1000) bounds the worst case: 30,000 writes a month, inside the free tier.

## Without a payment card: Workers KV

In wrangler.toml replace the three `[[r2_buckets]]` lines with

    [[kv_namespaces]]
    binding = "REPORTS"
    id = "<id printed by: wrangler kv namespace create nebula-diag-reports>"

and in worker.mjs set `const PUT_OPTIONS = { expirationTtl: 180 * 86400 };` (KV deletes each report after 180 days).
KV free: 1 GB, 1,000 writes a day (keep `DAILY_CAP` at 1000 or below). Fetch with
`wrangler kv key list --binding REPORTS` and `wrangler kv key get --binding REPORTS <key> > <file>`.

## Tests

`node --test tools/telemetry/worker/worker.test.mjs` (Node 18+, no packages): routes, limits, cap, storage keys.
