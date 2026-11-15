import pathlib
import shutil
import subprocess
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
RAW_DIR = ROOT / "data" / "raw"
PACKS_RAW = ROOT / "data" / "packs_raw"
PACKS_OUT = ROOT / "data" / "packs"
DIST_PACKS = ROOT / "dist" / "data" / "packs"
COMPILER = ROOT / "build" / "dict_compiler.exe"

HEADERS = {"User-Agent": "LiteIME-DictBuilder/1.0"}

CURATED_CS_CN = [
    ("操作系统", "cao zuo xi tong", 80000),
    ("数据结构", "shu ju jie gou", 80000),
    ("计算机网络", "ji suan ji wang luo", 80000),
    ("计算机组成原理", "ji suan ji zu cheng yuan li", 60000),
    ("时间复杂度", "shi jian fu za du", 60000),
    ("空间复杂度", "kong jian fu za du", 60000),
    ("动态规划", "dong tai gui hua", 75000),
    ("贪心算法", "tan xin suan fa", 60000),
    ("深度优先搜索", "shen du you xian sou suo", 50000),
    ("广度优先搜索", "guang du you xian sou suo", 50000),
    ("二分查找", "er fen zhao zhao", 30000),
    ("二分查找", "er fen cha zhao", 70000),
    ("哈希表", "ha xi biao", 70000),
    ("红黑树", "hong hei shu", 65000),
    ("平衡二叉树", "ping heng er cha shu", 60000),
    ("拓扑排序", "tuo pu pai xu", 60000),
    ("最小生成树", "zui xiao sheng cheng shu", 60000),
    ("最短路径", "zui duan lu jing", 60000),
    ("滑动窗口", "hua dong chuang kou", 65000),
    ("双指针", "shuang zhi zhen", 65000),
    ("单调栈", "dan diao zhan", 60000),
    ("并查集", "bing cha ji", 65000),
    ("前缀和", "qian zhui he", 60000),
    ("线段树", "xian duan shu", 60000),
    ("字典树", "zi dian shu", 55000),
    ("虚拟内存", "xu ni nei cun", 70000),
    ("页表", "ye biao", 60000),
    ("缺页中断", "que ye zhong duan", 60000),
    ("死锁", "si suo", 70000),
    ("信号量", "xin hao liang", 65000),
    ("互斥锁", "hu chi suo", 65000),
    ("自旋锁", "zi xuan suo", 60000),
    ("读写锁", "du xie suo", 60000),
    ("条件变量", "tiao jian bian liang", 60000),
    ("上下文切换", "shang xia wen qie huan", 65000),
    ("系统调用", "xi tong diao yong", 65000),
    ("用户态", "yong hu tai", 60000),
    ("内核态", "nei he tai", 60000),
    ("进程调度", "jin cheng diao du", 60000),
    ("线程池", "xian cheng chi", 65000),
    ("协程", "xie cheng", 70000),
    ("异步编程", "yi bu bian cheng", 60000),
    ("阻塞队列", "zu se dui lie", 60000),
    ("内存泄漏", "nei cun xie lou", 70000),
    ("内存溢出", "nei cun yi chu", 65000),
    ("垃圾回收", "la ji hui shou", 65000),
    ("拥塞控制", "yong se kong zhi", 65000),
    ("流量控制", "liu liang kong zhi", 65000),
    ("三次握手", "san ci wo shou", 70000),
    ("四次挥手", "si ci hui shou", 70000),
    ("滑动窗口协议", "hua dong chuang kou xie yi", 50000),
    ("子网掩码", "zi wang yan ma", 65000),
    ("路由表", "lu you biao", 60000),
    ("反向代理", "fan xiang dai li", 65000),
    ("负载均衡", "fu zai jun heng", 70000),
    ("微服务", "wei fu wu", 70000),
    ("消息队列", "xiao xi dui lie", 70000),
    ("分布式系统", "fen bu shi xi tong", 65000),
    ("一致性哈希", "yi zhi xing ha xi", 60000),
    ("索引优化", "suo yin you hua", 60000),
    ("事务隔离级别", "shi wu ge li ji bie", 55000),
    ("读写分离", "du xie fen li", 60000),
    ("机器学习", "ji qi xue xi", 80000),
    ("深度学习", "shen du xue xi", 80000),
    ("大语言模型", "da yu yan mo xing", 75000),
    ("大模型", "da mo xing", 80000),
    ("注意力机制", "zhu yi li ji zhi", 70000),
    ("反向传播", "fan xiang chuan bo", 65000),
    ("梯度下降", "ti du xia jiang", 70000),
    ("过拟合", "guo ni he", 65000),
    ("正则化", "zheng ze hua", 65000),
    ("向量数据库", "xiang liang shu ju ku", 65000),
    ("检索增强生成", "jian suo zeng qiang sheng cheng", 60000),
    ("上下文窗口", "shang xia wen chuang kou", 60000),
    ("提示词工程", "ti shi ci gong cheng", 65000),
    ("单元测试", "dan yuan ce shi", 70000),
    ("集成测试", "ji cheng ce shi", 65000),
    ("持续集成", "chi xu ji cheng", 60000),
    ("依赖注入", "yi lai zhu ru", 65000),
    ("面向对象", "mian xiang dui xiang", 75000),
    ("设计模式", "she ji mo shi", 75000),
    ("单例模式", "dan li mo shi", 65000),
    ("工厂模式", "gong chang mo shi", 65000),
    ("观察者模式", "guan cha zhe mo shi", 60000),
    ("输入法框架", "shu ru fa kuang jia", 60000),
]

CURATED_CS_EN = [
    ("Kubernetes", 90000),
    ("PostgreSQL", 90000),
    ("TypeScript", 95000),
    ("JavaScript", 95000),
    ("PyTorch", 90000),
    ("TensorFlow", 85000),
    ("Transformer", 85000),
    ("WebSocket", 85000),
    ("gRPC", 80000),
    ("GraphQL", 80000),
    ("Docker", 95000),
    ("Redis", 95000),
    ("Nginx", 90000),
    ("CMake", 85000),
    ("LLVM", 80000),
    ("Rust", 90000),
    ("Golang", 85000),
    ("FastAPI", 80000),
    ("Asyncio", 75000),
    ("Direct2D", 75000),
    ("Win32", 80000),
]


def fetch(url: str, dst: pathlib.Path) -> pathlib.Path:
    if dst.exists() and dst.stat().st_size > 0:
        print(f"Cached: {dst.name}")
        return dst
    print(f"Downloading {url} -> {dst.name}...")
    req = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=30) as resp, open(dst, "wb") as f:
        shutil.copyfileobj(resp, f)
    return dst


def build_char_pinyin_map() -> tuple[dict[str, str], dict[str, str]]:
    phrase_py: dict[str, str] = {}
    char_py: dict[str, str] = {}
    char_freq: dict[str, int] = {}

    for fname in ("8105.dict.yaml", "base.dict.yaml"):
        p = RAW_DIR / fname
        if not p.exists():
            continue
        past = False
        with open(p, "r", encoding="utf-8", errors="ignore") as f:
            first = f.readline().strip()
            if first != "---":
                past = True
                f.seek(0)
            for line in f:
                line = line.strip()
                if not past:
                    if line == "...":
                        past = True
                    continue
                if not line or line.startswith("#"):
                    continue
                parts = line.split("\t")
                if len(parts) < 2:
                    continue
                w, py = parts[0].strip(), parts[1].strip()
                freq = int(parts[2]) if len(parts) >= 3 and parts[2].isdigit() else 50
                if not w or not py:
                    continue
                if len(w) == 1:
                    if freq >= char_freq.get(w, -1):
                        char_freq[w] = freq
                        char_py[w] = py
                elif w not in phrase_py:
                    phrase_py[w] = py
    return char_py, phrase_py


def annotate_wordlist_to_tsv(src_txt: pathlib.Path, out_tsv: pathlib.Path,
                             char_py: dict[str, str], phrase_py: dict[str, str]) -> int:
    kept = 0
    with open(src_txt, "r", encoding="utf-8", errors="ignore") as fin, \
         open(out_tsv, "w", encoding="utf-8") as fout:
        for line in fin:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            word = parts[0].strip()
            freq = parts[1] if len(parts) > 1 and parts[1].isdigit() else "100"
            if len(word) < 2 or len(word) > 10:
                continue
            if word in phrase_py:
                fout.write(f"{word}\t{phrase_py[word]}\t{freq}\n")
                kept += 1
                continue
            pys = [char_py.get(ch) for ch in word]
            if all(pys):
                fout.write(f"{word}\t{' '.join(pys)}\t{freq}\n")
                kept += 1
    return kept


def compile_pack(out_name: str, args: list[str]) -> None:
    out_path = PACKS_OUT / out_name
    cmd = [str(COMPILER), str(out_path)] + args
    print(f"Compiling {out_name}...")
    rc = subprocess.call(cmd)
    if rc != 0:
        raise RuntimeError(f"Failed compiling {out_name} (rc={rc})")
    DIST_PACKS.mkdir(parents=True, exist_ok=True)
    shutil.copy2(out_path, DIST_PACKS / out_name)


def main() -> int:
    PACKS_RAW.mkdir(parents=True, exist_ok=True)
    PACKS_OUT.mkdir(parents=True, exist_ok=True)
    if not COMPILER.exists():
        print(f"Missing {COMPILER}", file=sys.stderr)
        return 1

    # 1. Computer Science / IT pack
    thuocl_it = fetch("https://raw.githubusercontent.com/Dwsy/rime/main/THUOCL_IT.dict.yaml", PACKS_RAW / "THUOCL_IT.dict.yaml")
    luna_comp = fetch("https://raw.githubusercontent.com/renyijiu/rime/master/luna_pinyin.computer.dict.yaml", PACKS_RAW / "luna_pinyin.computer.dict.yaml")

    curated_cn = PACKS_RAW / "curated_cs_cn.tsv"
    with open(curated_cn, "w", encoding="utf-8") as f:
        for w, py, fr in CURATED_CS_CN:
            f.write(f"{w}\t{py}\t{fr}\n")

    curated_en = PACKS_RAW / "curated_cs_en.tsv"
    with open(curated_en, "w", encoding="utf-8") as f:
        for w, fr in CURATED_CS_EN:
            f.write(f"{w}\t{w.lower()}\t{fr}\n")

    compile_pack("computer.dict", [
        "--cn", str(thuocl_it),
        "--cn", str(luna_comp),
        "--cn", str(curated_cn),
        "--en", str(curated_en),
    ])

    # 2. Rime-Ice extended encyclopedia pack
    ext_yaml = fetch("https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/ext.dict.yaml", PACKS_RAW / "ext.dict.yaml")
    compile_pack("ext_cn.dict", ["--cn", str(ext_yaml)])

    # 3. Specialty fields pack: Finance + Medical + Law (from THUOCL annotated via 8105+base)
    char_py, phrase_py = build_char_pinyin_map()
    pro_args: list[str] = []
    for name, url in [
        ("THUOCL_caijing.txt", "https://raw.githubusercontent.com/thunlp/THUOCL/master/data/THUOCL_caijing.txt"),
        ("THUOCL_medical.txt", "https://raw.githubusercontent.com/thunlp/THUOCL/master/data/THUOCL_medical.txt"),
        ("THUOCL_law.txt", "https://raw.githubusercontent.com/thunlp/THUOCL/master/data/THUOCL_law.txt"),
    ]:
        raw_txt = fetch(url, PACKS_RAW / name)
        tsv_path = PACKS_RAW / (name + ".tsv")
        cnt = annotate_wordlist_to_tsv(raw_txt, tsv_path, char_py, phrase_py)
        print(f"Annotated {name}: {cnt} entries")
        pro_args.extend(["--cn", str(tsv_path)])
    compile_pack("pro_fields.dict", pro_args)

    print(f"All domain packs staged in {DIST_PACKS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
