# QArchiver 待解决问题

更新日期：2026-11-27
来源：`code-review-2026-11-26.md`（三轮修复后的剩余项）

已完成的部分不再重复，只保留仍然存在、或明确推迟的问题。修复记录见
`code-review-2026-11-26.md` 第 9 节。

---

## 3. CI 与测试工具（已明确推迟）

按当前约定，不要动 CI 和 Windows 测试子集。记录备查：

- **Sanitizer job**：本地已有 `-DQARCHIVER_ENABLE_SANITIZERS=ON`（ASan+UBSan，
  GCC/Clang）。建议单独 job：

  ```bash
  cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
        -DBUILD_TESTING=ON -DQARCHIVER_USE_LIBARCHIVE=OFF \
        -DQARCHIVER_ENABLE_SANITIZERS=ON
  cmake --build build-asan -j
  QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 \
        ctest --test-dir build-asan --output-on-failure
  ```

  已在本机用 Qt 6.8.2 验证 `tst_utils`、`tst_manager`、`tst_preferences`
  在 ASan/UBSan 下通过。
- **clang-tidy**：可先作为独立非阻塞 job，从 `src/core`、`src/utils` 开始，
  避免一次引入大量历史告警。
- **`-Werror`**：当前 `qarchiver_set_warnings()` 只开
  `-Wall -Wextra -Wpedantic`（MSVC `/W4 /permissive-`），未升级为错误。
  建议先修完现有告警，再在 CI 的 Linux job 上开启。
- **Windows 测试子集**：仍为
  `tst_utils|tst_toolchain|tst_background|tst_preferences`；扩大前需要先确认
  `tst_navstate` / `tst_preview` / `tst_dragout` 在 offscreen + 内置 7-Zip 下
  的 Windows 行为。

---

## 4. 其他后续项（低优先）

1. **ArchiveManager 退出等待**
   - `shutdown()` 已删除 `QThread::terminate()`，15 秒超时后改为继续等待；
   - CLI 进程在取消后会由 `ProcessRunner` 先 `terminate()`、再 `kill()`，
     因此普通辅助进程不会无限阻塞退出；
   - 若将来某个后端（例如不轮询取消的进程内读取路径）卡死，退出仍会一直
     等待。暂不引入全局硬超时：解压/压缩本身可能合法地运行很久，误杀风险
     高于收益。若需要，可增加可配置的 helper 超时，或在退出前提示用户。

---

## 5. 快速验证命令

```bash
# 本机（Qt 6.4 + libarchive）
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

# 模拟 Windows 配置（Qt 6.8 + 无 libarchive）
cmake -S . -B build-qt68 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DQARCHIVER_USE_LIBARCHIVE=OFF -DCMAKE_PREFIX_PATH=<Qt-6.8-prefix>
cmake --build build-qt68 -j
QT_QPA_PLATFORM=offscreen ctest --test-dir build-qt68 --output-on-failure
```

当前基线（2026-11-27）：

- Qt 6.4.2 + libarchive：16/16 通过（新增 `tst_oneclick`）；
- Qt 6.4.2 + libarchive off：15/15 通过（同上，无 `tst_libarchive`）；
- Qt 6.8.2 + ASan/UBSan（上一轮记录）：`tst_manager` 12 passed / 4 skipped，
  `tst_preferences` 24 passed / 2 skipped。
