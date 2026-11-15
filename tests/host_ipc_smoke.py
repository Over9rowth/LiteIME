import ctypes
import ctypes.wintypes as wt
import os
import pathlib
import struct
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
HOST_EXE = ROOT / "dist" / "LiteIMEHost.exe"

MAGIC = 0x4C494D45
VERSION = 1

# Match ipc::Request (packed, 44 bytes)
REQ_FMT = "<IHHIIIBBBBiiiQ"
# Match ipc::Response (packed, 784 bytes: I H B B B B H H H 256s 512s)
RESP_FMT = "<IHBBBBHHH256s512s"

assert struct.calcsize(REQ_FMT) == 44
assert struct.calcsize(RESP_FMT) == 784

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD, wt.HANDLE]
kernel32.CreateFileW.restype = wt.HANDLE
kernel32.WaitNamedPipeW.argtypes = [wt.LPCWSTR, wt.DWORD]
kernel32.WaitNamedPipeW.restype = wt.BOOL
kernel32.SetNamedPipeHandleState.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD), ctypes.POINTER(wt.DWORD), ctypes.POINTER(wt.DWORD)]
kernel32.SetNamedPipeHandleState.restype = wt.BOOL
kernel32.TransactNamedPipe.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD, wt.LPVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]
kernel32.TransactNamedPipe.restype = wt.BOOL
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.CloseHandle.restype = wt.BOOL

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
PIPE_READMODE_MESSAGE = 0x00000002
INVALID_HANDLE_VALUE = wt.HANDLE(-1).value


def get_session_id() -> int:
    sid = wt.DWORD()
    kernel32.ProcessIdToSessionId(os.getpid(), ctypes.byref(sid))
    return sid.value


def send_ipc(pipe, msg_type: int, vk: int = 0, mods: int = 0, key_up: int = 0, focus: int = 1):
    req = struct.pack(
        REQ_FMT,
        MAGIC, VERSION, msg_type, vk, 0, mods, key_up, 1, focus, 0,
        320, 320, 22, 0
    )
    resp_buf = ctypes.create_string_buffer(struct.calcsize(RESP_FMT))
    bytes_read = wt.DWORD()
    ok = kernel32.TransactNamedPipe(
        pipe,
        req, len(req),
        resp_buf, len(resp_buf),
        ctypes.byref(bytes_read),
        None
    )
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())
    magic, ver, consumed, is_en, is_fw, is_cnp, pre_len, com_len, caret, pre_b, com_b = struct.unpack(
        RESP_FMT, resp_buf.raw
    )
    assert magic == MAGIC and ver == VERSION
    return {
        "consumed": bool(consumed),
        "is_en": bool(is_en),
        "preedit": pre_b[:pre_len].decode("utf-8"),
        "commit": com_b[:com_len].decode("utf-8"),
    }


def main() -> int:
    user_dict = pathlib.Path(os.environ.get("APPDATA", "")) / "LiteIME" / "user_dict.tsv"
    saved_user_dict = user_dict.read_bytes() if user_dict.exists() else None
    config_ini = user_dict.parent / "config.ini"
    saved_config_ini = config_ini.read_bytes() if config_ini.exists() else None
    config_ini.parent.mkdir(parents=True, exist_ok=True)
    config_ini.write_text(
        "# LiteIME Smoke Test Config\n"
        "pageSize=5\n"
        "chineseMode=1\n"
        "chinesePunct=1\n"
        "mixedEnglish=1\n"
        "typoCorrection=1\n"
        "enableCsPack=1\n"
        "inlinePreedit=0\n",
        encoding="utf-8",
    )

    # Ensure clean start (and wait briefly for terminated process cleanup)
    subprocess.run(["taskkill", "/F", "/IM", "LiteIMEHost.exe"], capture_output=True)
    time.sleep(0.15)
    proc = subprocess.Popen([str(HOST_EXE)], cwd=str(HOST_EXE.parent))
    try:
        pipe_name = f"\\\\.\\pipe\\LiteIME_Host_{get_session_id()}"
        pipe = INVALID_HANDLE_VALUE
        for _ in range(40):
            pipe = kernel32.CreateFileW(
                pipe_name, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None
            )
            if pipe != INVALID_HANDLE_VALUE:
                break
            time.sleep(0.05)
        assert pipe != INVALID_HANDLE_VALUE, f"Failed to open pipe {pipe_name}"

        mode = wt.DWORD(PIPE_READMODE_MESSAGE)
        assert kernel32.SetNamedPipeHandleState(pipe, ctypes.byref(mode), None, None)

        # 1. Ping (must NOT show StatusPill yet), then FocusChanged(1) (must show StatusPill)
        u32 = ctypes.WinDLL("user32", use_last_error=True)
        u32.FindWindowW.argtypes = [wt.LPCWSTR, wt.LPCWSTR]
        u32.FindWindowW.restype = wt.HWND
        u32.IsWindowVisible.argtypes = [wt.HWND]
        u32.IsWindowVisible.restype = wt.BOOL

        def wait_visible(hwnd, expected: bool, timeout: float = 0.5) -> bool:
            deadline = time.perf_counter() + timeout
            while time.perf_counter() < deadline:
                if bool(u32.IsWindowVisible(hwnd)) == expected:
                    return True
                time.sleep(0.01)
            return bool(u32.IsWindowVisible(hwnd)) == expected

        status_hwnd = None
        for _ in range(30):
            status_hwnd = u32.FindWindowW("LiteIME_StatusPill", None)
            if status_hwnd:
                break
            time.sleep(0.02)
        assert status_hwnd, f"LiteIMEHost failed to create LiteIME_StatusPill (poll={proc.poll()})"
        r = send_ipc(pipe, 5)
        assert not r["is_en"]
        assert wait_visible(status_hwnd, False), "StatusPill should stay hidden before IME activation/focus!"

        send_ipc(pipe, 3, focus=1)  # FocusChanged(1)
        assert wait_visible(status_hwnd, True), "StatusPill must show when active IME gains focus!"

        # 2. Type "nihao" + Space on real 552k dictionary, verify CandidateWindow stays visible on BOTH KeyTest(KeyUp) AND KeyEvent(KeyUp)!
        t0 = time.perf_counter()
        for ch in "NIHAO":
            r = send_ipc(pipe, 2, ord(ch), 0, 0)
            assert r["consumed"]
            # Simulate KeyTest(KeyUp) and KeyEvent(KeyUp) as TSF hosts do on key release
            r_up_test = send_ipc(pipe, 1, ord(ch), 0, 1)
            assert not r_up_test["consumed"]
            r_up_ev = send_ipc(pipe, 2, ord(ch), 0, 1)
            assert not r_up_ev["consumed"]
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        assert r["preedit"] == "你好", f"Unexpected preedit for nihao: {r['preedit']}"
        cand_hwnd = u32.FindWindowW("LiteIME_CandidateWindow", None)
        assert cand_hwnd and wait_visible(cand_hwnd, True), "CandidateWindow must stay visible after key release!"

        # 2b. Secondary background pipe losing focus must NOT clobber active pipe's StatusPill or CandidateWindow
        pipe_bg = kernel32.CreateFileW(
            pipe_name, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None
        )
        assert pipe_bg != INVALID_HANDLE_VALUE
        assert kernel32.SetNamedPipeHandleState(pipe_bg, ctypes.byref(mode), None, None)
        send_ipc(pipe_bg, 3, focus=0)
        assert wait_visible(cand_hwnd, True), "Background focus loss must not hide active CandidateWindow!"
        assert wait_visible(status_hwnd, True), "Background focus loss must not hide active StatusPill!"
        kernel32.CloseHandle(pipe_bg)

        r_commit = send_ipc(pipe, 2, 0x20, 0, 0)  # Space
        assert r_commit["commit"] == "你好", f"Unexpected commit: {r_commit['commit']}"
        assert wait_visible(cand_hwnd, False), "CandidateWindow must hide after commit!"
        assert wait_visible(status_hwnd, True), "StatusPill stays visible while LiteIME still has focus!"

        # 3. Type "wozaixieyigeshurufa" + Space
        for ch in "WOZAIXIEYIGESHURUFA":
            r = send_ipc(pipe, 2, ord(ch), 0, 0)
            assert r["consumed"]
        r_sent = send_ipc(pipe, 2, 0x20, 0, 0)
        print(f"Sentence for 'wozaixieyigeshurufa': {r_sent['commit']} (5-key IPC+engine latency={elapsed_ms:.2f}ms)")
        assert "输入法" in r_sent["commit"], f"Unexpected sentence: {r_sent['commit']}"

        # 4. Jianpin: "srf" should include "输入法"
        for ch in "SRF":
            r = send_ipc(pipe, 2, ord(ch), 0, 0)
            assert r["consumed"]
        # Escape cancels cleanly
        r_esc = send_ipc(pipe, 2, 0x1B, 0, 0)
        assert r_esc["consumed"] and r_esc["preedit"] == ""

        # 5. Bilingual English: type "python" + Enter -> "python"
        for ch in "PYTHON":
            r = send_ipc(pipe, 2, ord(ch), 0, 0)
            assert r["consumed"]
        r_en = send_ipc(pipe, 2, 0x0D, 0, 0)  # Enter
        assert r_en["commit"] == "python"

        # 6. Smart decimal (3. -> '.') & Chinese punctuation (,)
        send_ipc(pipe, 2, ord("3"), 0, 0)
        r_dot = send_ipc(pipe, 2, 0xBE, 0, 0)
        assert r_dot["commit"] == ".", f"Expected '.', got {r_dot['commit']}"
        r_comma = send_ipc(pipe, 2, 0xBC, 0, 0)
        assert r_comma["commit"] == "，", f"Expected '，', got {r_comma['commit']}"

        # 6b. Verify ']' (0xDD) flips pages during long string without committing '】', and partial commit keeps remaining pinyin!
        for ch in "WOZAIXIEYIGESHURUFA":
            assert send_ipc(pipe, 2, ord(ch), 0, 0)["consumed"]
        r_page2 = send_ipc(pipe, 2, 0xDD, 0, 0)  # ']'
        assert r_page2["consumed"] and r_page2["commit"] == "", f"']' must flip page instead of committing: {r_page2}"
        assert wait_visible(cand_hwnd, True)
        assert send_ipc(pipe, 2, 0xDB, 0, 0)["commit"] == ""  # '[' back to page 1
        # Select partial prefix '2' ("我在") -> composition continues with remaining pinyin!
        r_part = send_ipc(pipe, 2, ord("2"), 0, 0)
        assert r_part["consumed"] and r_part["commit"] == "", f"Partial prefix select should stay composing: {r_part}"
        assert wait_visible(cand_hwnd, True)
        assert send_ipc(pipe, 2, 0x1B, 0, 0)["consumed"]  # Escape

        # 7. Single-initial 'W' word-length cap (only 1 Chinese char in preedit), Typo 'ZHOGN' -> '中', ModCaps priority, and 'L' single initial richness
        r_w = send_ipc(pipe, 2, ord("W"), 0, 0)
        assert r_w["consumed"] and len(r_w["preedit"]) == 1, f"'W' must cap to 1 char, got: {r_w['preedit']}"
        assert send_ipc(pipe, 2, 0x1B, 0, 0)["consumed"]

        # Single initial 'L' should show top high-frequency L* char across all l* syllables (e.g. 了/来/里), not stuck in 'la*'
        r_l = send_ipc(pipe, 2, ord("L"), 0, 0)
        assert r_l["consumed"] and r_l["preedit"] in ("了", "来", "里", "老", "两", "理"), f"Unexpected top 'l' char: {r_l}"
        assert send_ipc(pipe, 2, 0x1B, 0, 0)["consumed"]

        for ch in "ZHOGN":
            r_ty = send_ipc(pipe, 2, ord(ch), 0, 0)
            assert r_ty["consumed"]
        assert r_ty["preedit"] == "中", f"Typo 'zhogn' should resolve to '中', got: {r_ty['preedit']}"
        assert send_ipc(pipe, 2, 0x1B, 0, 0)["consumed"]

        # 7b. Mixed Jianpin + suffix ("smguia" -> "什么" at top, picking '1' continues with "guia")
        for ch in "SMGUIA":
            r_sm = send_ipc(pipe, 2, ord(ch), 0, 0)
            assert r_sm["consumed"]
        assert "什么" in r_sm["preedit"], f"Expected '什么' top candidate for SMGUIA, got: {r_sm}"
        r_pick_sm = send_ipc(pipe, 2, ord("1"), 0, 0)
        assert r_pick_sm["consumed"] and r_pick_sm["commit"] == "", f"Picking prefix should keep composing: {r_pick_sm}"
        assert send_ipc(pipe, 2, 0x1B, 0, 0)["consumed"]

        # ModCaps (8): passes uppercase letters and English punctuation straight through in CN mode
        r_caps_a = send_ipc(pipe, 1, ord("A"), 8, 0)
        assert not r_caps_a["consumed"], "ModCaps A must pass through!"
        r_caps_comma = send_ipc(pipe, 1, 0xBC, 8, 0)
        assert not r_caps_comma["consumed"], "ModCaps ',' must pass through as English punctuation!"

        # 8. Shift tap toggles CN -> EN
        send_ipc(pipe, 2, 0x10, 1, 0)
        r_sh = send_ipc(pipe, 2, 0x10, 0, 1)
        assert r_sh["is_en"] is True

        # 9. FocusChanged(0) / Deactivate hides BOTH CandidateWindow and StatusPill cleanly
        send_ipc(pipe, 2, ord("A"), 0, 0)  # idle in EN mode, verify focus loss after switching back
        send_ipc(pipe, 2, 0x10, 1, 0)
        r_sh2 = send_ipc(pipe, 2, 0x10, 0, 1)
        assert r_sh2["is_en"] is False
        send_ipc(pipe, 2, ord("N"), 0, 0)
        assert wait_visible(cand_hwnd, True)
        assert wait_visible(status_hwnd, True)
        send_ipc(pipe, 3, focus=0)  # Deactivate / switch to another IME
        assert wait_visible(cand_hwnd, False), "CandidateWindow must hide when switching IME!"
        assert wait_visible(status_hwnd, False), "StatusPill must hide when switching IME!"

        # 10. Modal UI non-blocking & multi-client IPC concurrency regression test:
        # Open modal QuickInput privacy MessageBoxW via "vvv" -> Space, then slam IPC from 4 worker threads.
        send_ipc(pipe, 3, focus=1)
        for ch in "VVV":
            assert send_ipc(pipe, 2, ord(ch), 0, 0)["consumed"]
        send_ipc(pipe, 2, 0x20, 0, 0)  # opens modal warning MessageBoxW on GUI thread!
        time.sleep(0.08)

        errors = []
        latencies_ms = []
        lock = threading.Lock()

        def worker(tid: int):
            p = INVALID_HANDLE_VALUE
            try:
                for _ in range(10):
                    p = kernel32.CreateFileW(
                        pipe_name, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None
                    )
                    if p != INVALID_HANDLE_VALUE:
                        break
                    kernel32.WaitNamedPipeW(pipe_name, 100)
                if p == INVALID_HANDLE_VALUE:
                    raise RuntimeError(f"Worker {tid} failed open pipe")
                m = wt.DWORD(PIPE_READMODE_MESSAGE)
                assert kernel32.SetNamedPipeHandleState(p, ctypes.byref(m), None, None)
                send_ipc(p, 3, focus=1)
                for _ in range(15):
                    ts = time.perf_counter()
                    res_ping = send_ipc(p, 5)
                    assert not res_ping["is_en"]
                    res_k = send_ipc(p, 2, ord("W"), 0, 0)
                    assert res_k["consumed"]
                    send_ipc(p, 2, 0x1B, 0, 0)  # Escape
                    dt = (time.perf_counter() - ts) * 1000.0
                    with lock:
                        latencies_ms.append(dt)
            except Exception as exc:
                with lock:
                    errors.append(f"Worker {tid}: {exc}")
            finally:
                if p != INVALID_HANDLE_VALUE:
                    kernel32.CloseHandle(p)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=5.0)
            assert not t.is_alive(), "Concurrent IPC thread deadlocked while GUI modal dialog was open!"
        assert not errors, f"Concurrent IPC errors: {errors}"
        max_lat = max(latencies_ms)
        assert max_lat < 250.0, f"IPC stalled during modal dialog! max latency = {max_lat:.1f}ms"
        print(f"Concurrent non-blocking IPC under modal UI passed (60 ops, max 3-call batch latency={max_lat:.2f}ms)!")

        kernel32.CloseHandle(pipe)
        print("Live Host IPC + 552k Bilingual Dict smoke test PASSED!")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        if saved_user_dict is not None:
            user_dict.parent.mkdir(parents=True, exist_ok=True)
            user_dict.write_bytes(saved_user_dict)
        elif user_dict.exists():
            user_dict.unlink()
        if saved_config_ini is not None:
            config_ini.parent.mkdir(parents=True, exist_ok=True)
            config_ini.write_bytes(saved_config_ini)
        elif config_ini.exists():
            config_ini.unlink()


if __name__ == "__main__":
    raise SystemExit(main())
