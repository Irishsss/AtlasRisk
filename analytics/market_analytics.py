"""
Market metrics from the AtlasRisk trade and quote stream.

Reads market_events.csv (written by bench/market_replay) and prints spread,
VWAP, price impact, order-flow imbalance, and realized volatility.
Needs numpy and pandas.

CSV columns: ts, event, side, price, quantity, best_bid, best_ask
  event = 'trade' for an execution, 'quote' for a top-of-book update.
"""

import sys
import numpy as np
import pandas as pd


def load(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    # mid price is only defined when both sides are present
    df["mid"] = np.where(
        (df["best_bid"] > 0) & (df["best_ask"] > 0),
        (df["best_bid"] + df["best_ask"]) / 2.0,
        np.nan,
    )
    return df


def spread_stats(df: pd.DataFrame) -> dict:
    q = df[df["event"] == "quote"].copy()
    q = q[(q["best_bid"] > 0) & (q["best_ask"] > 0)]
    spread = q["best_ask"] - q["best_bid"]
    # spread in basis points of mid: (ask - bid) / mid * 10,000
    spread_bps = spread / ((q["best_ask"] + q["best_bid"]) / 2.0) * 1e4
    return {
        "mean_spread_ticks": float(spread.mean()),
        "median_spread_ticks": float(spread.median()),
        "mean_spread_bps": float(spread_bps.mean()),
    }


def vwap(df: pd.DataFrame) -> float:
    # Volume-weighted average trade price: sum(price*qty) / sum(qty).
    t = df[df["event"] == "trade"]
    if t["quantity"].sum() == 0:
        return float("nan")
    return float((t["price"] * t["quantity"]).sum() / t["quantity"].sum())


def order_flow_imbalance(df: pd.DataFrame) -> dict:
    # Signed traded volume: buy-initiated trades add, sell-initiated subtract.
    t = df[df["event"] == "trade"].copy()
    signed = np.where(t["side"] == "Buy", t["quantity"], -t["quantity"])
    total = t["quantity"].sum()
    return {
        "buy_volume": int(t.loc[t["side"] == "Buy", "quantity"].sum()),
        "sell_volume": int(t.loc[t["side"] == "Sell", "quantity"].sum()),
        "net_imbalance": int(signed.sum()),
        "imbalance_ratio": float(signed.sum() / total) if total else float("nan"),
    }


def price_impact(df: pd.DataFrame, horizon: int = 20) -> dict:
    # Price impact: mid `horizon` trades later minus the trade's reference
    # mid, signed by taker side, in bps.
    t = df[df["event"] == "trade"].copy().reset_index(drop=True)
    # forward-fill mid so every trade has a reference mid even between quotes
    t["mid_ff"] = t["mid"].ffill()
    if len(t) <= horizon:
        return {"mean_impact_bps": float("nan"), "sample": 0}

    future_mid = t["mid_ff"].shift(-horizon)
    direction = np.where(t["side"] == "Buy", 1.0, -1.0)
    impact = direction * (future_mid - t["mid_ff"]) / t["mid_ff"] * 1e4  # bps
    impact = impact.dropna()
    return {"mean_impact_bps": float(impact.mean()), "sample": int(impact.size)}


def realized_vol(df: pd.DataFrame) -> float:
    # Std dev of mid-price log returns, in bps.
    m = df["mid"].dropna()
    if len(m) < 3:
        return float("nan")
    logret = np.diff(np.log(m.values))
    return float(np.std(logret) * 1e4)  # in bps


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "market_events.csv"
    df = load(path)

    n_trades = int((df["event"] == "trade").sum())
    n_quotes = int((df["event"] == "quote").sum())

    print("AtlasRisk market analytics")
    print("==========================")
    print(f"events        : {len(df):,}  ({n_trades:,} trades, {n_quotes:,} quotes)")
    print()

    s = spread_stats(df)
    print("Liquidity")
    print(f"  mean spread : {s['mean_spread_ticks']:.2f} ticks "
          f"({s['mean_spread_bps']:.2f} bps)")
    print(f"  median      : {s['median_spread_ticks']:.2f} ticks")
    print()

    print("Execution")
    print(f"  VWAP        : {vwap(df):.2f}")
    pi = price_impact(df)
    print(f"  price impact: {pi['mean_impact_bps']:.2f} bps "
          f"(over {pi['sample']:,} trades)")
    print()

    ofi = order_flow_imbalance(df)
    print("Order flow")
    print(f"  buy volume  : {ofi['buy_volume']:,}")
    print(f"  sell volume : {ofi['sell_volume']:,}")
    print(f"  net imbalance: {ofi['net_imbalance']:,} "
          f"({ofi['imbalance_ratio']*100:.1f}% of volume)")
    print()

    print("Risk")
    print(f"  realized vol: {realized_vol(df):.2f} bps (per-tick log-return stdev)")


if __name__ == "__main__":
    main()
