import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PACKS_RAW = ROOT / "data" / "packs_raw"
PACKS_OUT = ROOT / "data" / "packs"
DIST_PACKS = ROOT / "dist" / "data" / "packs"
COMPILER = ROOT / "build" / "dict_compiler.exe"
RAW_EN_DICT = ROOT / "data" / "raw" / "en.dict.yaml"

# Curated high-frequency everyday, office, study, and digital English vocabulary.
# Frequencies >= 50,000 ensure genuine common words rank above flat-100 entries in base en.dict.yaml.
TOP_CORE_WORDS = [
    ("OK", 250000), ("ok", 240000), ("hello", 240000), ("hi", 210000), ("yes", 230000), ("no", 220000),
    ("thanks", 220000), ("thank", 200000), ("please", 210000), ("sorry", 210000), ("good", 235000),
    ("great", 210000), ("nice", 210000), ("cool", 200000), ("fine", 190000), ("welcome", 190000),
    ("you", 230000), ("your", 200000), ("we", 210000), ("our", 190000), ("me", 200000), ("my", 200000),
    ("he", 190000), ("she", 190000), ("they", 200000), ("them", 185000), ("their", 185000),
    ("this", 220000), ("that", 220000), ("these", 180000), ("those", 180000),
    ("what", 220000), ("when", 210000), ("where", 210000), ("who", 205000), ("why", 210000), ("how", 215000),
    ("which", 195000), ("can", 215000), ("will", 210000), ("would", 195000), ("could", 195000),
    ("should", 200000), ("must", 190000), ("may", 185000), ("might", 180000),
    ("do", 210000), ("does", 185000), ("did", 185000), ("done", 205000),
    ("go", 210000), ("going", 185000), ("gone", 170000), ("went", 170000),
    ("get", 210000), ("got", 190000), ("set", 200000), ("make", 215000), ("made", 185000),
    ("take", 195000), ("give", 195000), ("keep", 190000), ("put", 185000), ("let", 190000),
    ("have", 220000), ("has", 195000), ("had", 185000), ("need", 210000), ("want", 205000),
    ("like", 220000), ("love", 210000), ("hope", 190000), ("wish", 180000), ("feel", 190000),
    ("know", 215000), ("think", 210000), ("believe", 180000), ("understand", 185000), ("remember", 185000),
    ("learn", 200000), ("study", 200000), ("read", 200000), ("write", 200000), ("speak", 180000),
    ("listen", 180000), ("look", 205000), ("see", 205000), ("watch", 190000), ("find", 205000),
    ("check", 210000), ("test", 215000), ("try", 200000), ("use", 215000), ("work", 220000),
    ("play", 200000), ("start", 210000), ("stop", 205000), ("open", 210000), ("close", 205000),
    ("run", 210000), ("wait", 200000), ("help", 215000), ("ask", 195000), ("tell", 195000), ("say", 195000),
    ("show", 205000), ("hide", 185000), ("add", 205000), ("edit", 200000), ("save", 210000),
    ("delete", 200000), ("remove", 195000), ("update", 205000), ("create", 200000), ("build", 205000),
    ("fix", 205000), ("change", 200000), ("move", 190000), ("copy", 205000), ("paste", 200000),
    ("send", 205000), ("share", 200000), ("search", 205000), ("select", 195000), ("click", 200000),
    ("download", 205000), ("upload", 195000), ("install", 205000), ("login", 200000), ("logout", 185000),
    ("AI", 240000), ("API", 220000), ("APP", 210000), ("App", 215000), ("app", 215000),
    ("PDF", 210000), ("PPT", 210000), ("Word", 205000), ("Excel", 205000), ("Office", 200000),
    ("Windows", 220000), ("Mac", 195000), ("Linux", 205000), ("Android", 200000), ("iOS", 195000),
    ("iPhone", 200000), ("iPad", 195000), ("PC", 200000), ("CPU", 210000), ("GPU", 210000),
    ("USB", 200000), ("WiFi", 205000), ("HTTP", 195000), ("HTTPS", 195000), ("URL", 200000),
    ("HTML", 200000), ("CSS", 200000), ("JSON", 205000), ("SQL", 205000), ("Git", 210000),
    ("GitHub", 215000), ("VSCode", 215000), ("Python", 220000), ("Java", 215000), ("Rust", 205000),
    ("React", 205000), ("Vue", 200000), ("Node", 195000), ("Docker", 210000), ("Redis", 200000),
    ("English", 220000), ("Chinese", 215000), ("China", 210000), ("Google", 215000),
    ("true", 200000), ("false", 200000), ("null", 195000), ("none", 190000),
    ("in", 185000), ("on", 185000), ("at", 185000), ("to", 190000), ("for", 205000),
    ("from", 200000), ("with", 205000), ("about", 200000), ("after", 190000), ("before", 190000),
    ("and", 215000), ("or", 185000), ("but", 205000), ("if", 195000), ("else", 190000),
    ("not", 210000), ("all", 205000), ("one", 200000), ("two", 195000), ("new", 210000),
    ("old", 190000), ("first", 200000), ("last", 195000), ("next", 205000), ("back", 205000),
    ("time", 215000), ("today", 210000), ("tomorrow", 200000), ("yesterday", 195000),
    ("day", 200000), ("week", 195000), ("month", 195000), ("year", 205000),
    ("world", 220000), ("people", 205000), ("person", 190000), ("friend", 200000), ("family", 195000),
    ("home", 210000), ("school", 195000), ("company", 200000), ("team", 205000), ("group", 200000),
    ("user", 210000), ("admin", 195000), ("name", 215000), ("password", 205000), ("account", 205000),
    ("email", 215000), ("mail", 200000), ("phone", 205000), ("message", 205000), ("chat", 205000),
    ("file", 215000), ("folder", 200000), ("document", 195000), ("page", 210000), ("line", 205000),
    ("word", 210000), ("text", 210000), ("note", 205000), ("list", 210000), ("table", 200000),
    ("image", 205000), ("photo", 200000), ("video", 205000), ("audio", 195000), ("music", 205000),
    ("game", 205000), ("code", 220000), ("data", 220000), ("database", 205000), ("server", 205000),
    ("client", 200000), ("cloud", 205000), ("network", 200000), ("web", 210000), ("website", 200000),
    ("link", 205000), ("key", 205000), ("value", 205000), ("type", 205000), ("mode", 205000),
    ("config", 205000), ("setting", 200000), ("settings", 200000), ("status", 205000),
    ("error", 210000), ("warning", 195000), ("info", 200000), ("debug", 205000), ("log", 200000),
    ("bug", 210000), ("issue", 205000), ("task", 205000), ("plan", 205000), ("project", 210000),
    ("report", 200000), ("meeting", 200000), ("paper", 200000), ("review", 205000),
    ("model", 210000), ("agent", 205000), ("prompt", 205000), ("token", 205000),
    ("computer", 215000), ("system", 210000), ("software", 205000), ("hardware", 195000),
    ("program", 205000), ("service", 205000), ("version", 205000), ("release", 200000),
    ("commit", 205000), ("branch", 200000), ("merge", 200000), ("push", 200000), ("pull", 200000),
    ("screen", 200000), ("window", 205000), ("keyboard", 200000), ("mouse", 200000),
    ("color", 200000), ("theme", 200000), ("font", 200000), ("size", 200000), ("style", 200000),
    ("life", 205000), ("way", 200000), ("thing", 200000), ("problem", 205000), ("question", 205000),
    ("answer", 205000), ("idea", 205000), ("reason", 195000), ("result", 205000), ("point", 200000),
    ("part", 195000), ("place", 195000), ("case", 200000), ("example", 200000), ("step", 200000),
    ("number", 200000), ("level", 200000), ("order", 200000), ("power", 200000), ("speed", 200000),
    ("money", 200000), ("price", 195000), ("shop", 195000), ("buy", 195000), ("pay", 200000),
    ("food", 195000), ("water", 195000), ("coffee", 200000), ("tea", 195000),
    ("book", 205000), ("news", 200000), ("story", 195000), ("movie", 200000), ("song", 195000),
    ("happy", 205000), ("busy", 195000), ("easy", 205000), ("hard", 195000), ("fast", 200000),
    ("slow", 190000), ("high", 200000), ("low", 195000), ("big", 200000), ("small", 200000),
    ("long", 200000), ("short", 195000), ("right", 205000), ("left", 200000), ("wrong", 200000),
    ("best", 210000), ("better", 205000), ("more", 205000), ("most", 200000), ("less", 195000),
    ("many", 200000), ("much", 200000), ("some", 205000), ("any", 200000), ("every", 200000),
    ("other", 200000), ("same", 205000), ("different", 200000), ("important", 200000),
    ("possible", 195000), ("available", 195000), ("ready", 200000), ("clear", 200000),
    ("simple", 200000), ("special", 195000), ("public", 200000), ("private", 200000),
    ("local", 200000), ("global", 200000), ("online", 205000), ("offline", 195000),
    ("free", 205000), ("full", 200000), ("empty", 195000), ("clean", 200000),
    ("always", 200000), ("never", 200000), ("often", 195000), ("sometimes", 190000),
    ("already", 200000), ("still", 200000), ("just", 205000), ("only", 205000), ("also", 205000),
    ("very", 205000), ("really", 205000), ("actually", 195000), ("maybe", 200000), ("probably", 195000),
    ("here", 205000), ("there", 205000), ("now", 210000), ("then", 200000), ("soon", 200000),
    ("again", 200000), ("together", 195000), ("please", 205000), ("because", 200000),
    ("although", 185000), ("however", 190000), ("therefore", 185000), ("between", 190000),
    ("during", 190000), ("without", 195000), ("through", 195000), ("against", 185000),
    ("support", 205000), ("feature", 205000), ("design", 205000), ("layout", 200000),
    ("render", 195000), ("action", 200000), ("event", 200000), ("state", 200000),
    ("class", 200000), ("function", 205000), ("method", 200000), ("object", 200000),
    ("array", 200000), ("string", 205000), ("list", 205000), ("map", 200000),
    ("import", 200000), ("export", 200000), ("return", 205000), ("async", 200000),
    ("await", 200000), ("print", 200000), ("input", 205000), ("output", 205000),
    ("format", 200000), ("parse", 195000), ("match", 200000), ("sort", 200000),
    ("filter", 200000), ("count", 200000), ("index", 200000), ("cache", 200000),
    ("queue", 195000), ("stack", 200000), ("thread", 200000), ("process", 200000),
    ("memory", 205000), ("disk", 195000), ("port", 200000), ("host", 200000),
    ("path", 205000), ("root", 200000), ("home", 205000), ("script", 200000),
    ("command", 200000), ("terminal", 200000), ("shell", 195000), ("package", 200000),
    ("module", 200000), ("library", 200000), ("framework", 200000), ("plugin", 200000),
    ("tool", 205000), ("tools", 200000), ("editor", 200000), ("browser", 205000),
    ("chrome", 205000), ("edge", 200000), ("firefox", 190000), ("safari", 190000),
    ("office", 200000), ("notion", 195000), ("obsidian", 195000), ("markdown", 200000),
    ("wechat", 205000), ("qq", 195000), ("bilibili", 195000), ("deepseek", 205000),
    ("chatgpt", 205000), ("claude", 205000), ("gemini", 205000), ("openai", 200000),
]

# Supplement from rime-ice en.dict.yaml (~3+ letter pure alphabetic common words) at freq=50000
# so prefix completion covers broad everyday English without short 2-letter pinyin noise.
SKIP_SHORT = {
    "ba", "bo", "bi", "bu", "pa", "po", "pi", "pu", "ma", "mo", "mi", "mu",
    "fa", "fo", "fu", "da", "de", "di", "du", "ta", "te", "ti", "tu",
    "na", "ne", "ni", "nu", "la", "le", "li", "lu", "ga", "ge", "gu",
    "ka", "ke", "ku", "ha", "he", "hu", "ji", "qi", "xi", "za", "ze", "zi", "zu",
    "ca", "ce", "ci", "cu", "sa", "se", "si", "su", "wa", "wo", "wu",
    "ya", "ye", "yi", "yo", "yu", "er", "en", "ai", "ao", "ou",
}


def main() -> int:
    if not COMPILER.exists():
        print(f"Missing {COMPILER}", file=sys.stderr)
        return 1

    PACKS_RAW.mkdir(parents=True, exist_ok=True)
    PACKS_OUT.mkdir(parents=True, exist_ok=True)
    DIST_PACKS.mkdir(parents=True, exist_ok=True)

    seen_lower: dict[str, tuple[str, int]] = {}
    for w, fr in TOP_CORE_WORDS:
        lw = w.lower()
        # Allow distinct case variants only if case differs meaningfully like OK vs ok
        if w not in seen_lower:
            seen_lower[w] = (w, fr)

    core_lower = {w.lower() for w, _ in TOP_CORE_WORDS}

    # Include top clean 4+ letter alphabetic words from en.dict.yaml (skip obscure symbols)
    added_extra = 0
    if RAW_EN_DICT.exists():
        past_yaml = False
        with open(RAW_EN_DICT, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                s = line.strip()
                if not s:
                    continue
                if s == "---":
                    past_yaml = False
                    continue
                if s == "...":
                    past_yaml = True
                    continue
                if not past_yaml and ":" in s and "\t" not in s:
                    continue
                word = s.split("\t", 1)[0].strip()
                lw = word.lower()
                if (4 <= len(word) <= 14 and word.isalpha() and lw not in core_lower
                        and lw not in SKIP_SHORT and word not in seen_lower):
                    # Moderate boost (1500) so curated TOP_CORE_WORDS (180k-250k) dominate prefixes
                    seen_lower[word] = (word, 1500)
                    added_extra += 1
                    if added_extra >= 1800:
                        break

    raw_out = PACKS_RAW / "common_en.tsv"
    with open(raw_out, "w", encoding="utf-8") as f:
        for w, fr in seen_lower.values():
            f.write(f"{w}\t{w.lower()}\t{fr}\n")

    dict_out = PACKS_OUT / "common_en.dict"
    rc = subprocess.call([str(COMPILER), str(dict_out), "--en", str(raw_out)])
    if rc != 0:
        return rc

    shutil.copy2(dict_out, DIST_PACKS / "common_en.dict")
    print(f"Built common_en.dict ({len(seen_lower)} English entries) -> {DIST_PACKS / 'common_en.dict'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
