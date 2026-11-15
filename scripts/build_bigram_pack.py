import pathlib
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent
RAW_BASE = ROOT / "data" / "raw" / "base.dict.yaml"
OUT_BIGRAM = ROOT / "data" / "bigram.tsv"


def parse_dict_yaml(path: pathlib.Path):
    word_freq = {}
    in_body = False
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not in_body:
                if line == "...":
                    in_body = True
                continue
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            word = parts[0].strip()
            if not word or any(ord(c) < 128 for c in word):
                continue
            freq = 1
            if len(parts) >= 3:
                try:
                    freq = int(parts[2])
                except ValueError:
                    freq = 1
            if freq > 0:
                prev = word_freq.get(word, 0)
                if freq > prev:
                    word_freq[word] = freq
    return word_freq


def main() -> int:
    if not RAW_BASE.exists():
        print(f"Missing {RAW_BASE}")
        return 1

    word_freq = parse_dict_yaml(RAW_BASE)
    print(f"Loaded {len(word_freq)} unique Chinese words from base.dict.yaml")

    # Build bigram transition counts from high-frequency 2..6 char phrases split into subwords
    # Prefer meaningful 2+2, 1+2, 2+1 splits and high-freq 1+1 transitions.
    pair_weights = defaultdict(float)
    vocab = set(word_freq.keys())

    for phrase, freq in word_freq.items():
        n = len(phrase)
        if n < 2 or n > 6 or freq < 25:
            continue
        for split in range(1, n):
            left = phrase[:split]
            right = phrase[split:]
            if left not in vocab or right not in vocab:
                continue
            # Require at least one side >= 2 chars unless high frequency 2-char phrase
            if len(left) == 1 and len(right) == 1 and freq < 300:
                continue
            weight = float(freq) * (1.25 if (len(left) >= 2 and len(right) >= 2) else 1.0)
            pair_weights[(left, right)] += weight

    top_pairs = sorted(pair_weights.items(), key=lambda kv: kv[1], reverse=True)[:35000]
    with OUT_BIGRAM.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# LiteIME Statistical Bigram Pack (prevWord\\tcurWord\\tweight)\n")
        for (w1, w2), wt in top_pairs:
            f.write(f"{w1}\t{w2}\t{int(wt)}\n")

    print(f"Wrote {len(top_pairs)} statistical bigrams to {OUT_BIGRAM}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
