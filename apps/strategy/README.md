# Didrachma strategy CLI

The default CTest suite is offline. To opt in to the Yahoo Finance smoke test, configure with
`-DDIDRACHMA_ENABLE_YAHOO_STRATEGY_SMOKE=ON` and run:

```sh
ctest --test-dir build -R '^Test_Didrachma_apps_strategy_YahooSmoke$' --output-on-failure
```

The equivalent direct command is:

```sh
Didrachma_apps_strategy \
  --strategy apps/strategy/examples/aapl-yahoo-smoke.strategy.json \
  --subject AAPL \
  --from 2025-01-02T00:00:00Z \
  --through 2025-01-31T23:59:59Z \
  --provider yahoo
```

When Yahoo is reachable and returns the requested history, the input summary includes:

```text
Input subject-daily: AAPL @ 1 day [ready]
```
