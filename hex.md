**Objective**

Implement a fully functional hex editor for Qt 6.10 using low-level POSIX I/O, starting from a minimal mmap-based skeleton, and extend it with editing, safety, UI, and large-file support.

---

## 1. Data model and core editing

1. Replace the simple “mmap + fixed buffer” model with a model that supports variable file length

   * Use either:

     * A dynamic in-memory buffer, or
     * A paged/chunked model that can grow/shrink
   * Ensure the model supports insert and delete operations at arbitrary byte offsets

2. Implement insert/delete semantics in the model

   * Insert:

     * Shift all bytes at and after the insertion offset forward
     * Increase logical file size
   * Delete:

     * Remove the specified range of bytes
     * Shift subsequent bytes backward
     * Decrease logical file size
   * Keep offsets consistent and update any cached mappings

3. Keep a clear separation between:

   * The **logical file view** (what the user sees and edits)
   * The **backing store** (original file on disk, temp file, or mmap region)

---

## 2. Undo/redo system

4. Introduce an edit operation log

   * Define a struct for operations containing:

     * Operation type (overwrite, insert, delete)
     * Offset
     * Length
     * Old data (bytes before operation)
     * New data (bytes after operation), where applicable

5. Maintain two stacks:

   * Undo stack: push each applied operation
   * Redo stack: cleared when a new operation is performed after undo

6. Implement undo:

   * Pop last operation from undo stack
   * Apply the inverse operation to the model:

     * Overwrite → restore old data
     * Insert → delete the inserted range
     * Delete → reinsert the old data
   * Push inverse operation onto redo stack

7. Implement redo:

   * Pop last operation from redo stack
   * Reapply it to the model
   * Push original operation back onto undo stack

8. Ensure all insert/delete operations adjust subsequent recorded offsets or store offsets in a way that remains valid after earlier operations (e.g. apply undo/redo in strict reverse chronological order).

---

## 3. Large-file support (paged / windowed I/O)

9. Do not load or mmap the entire file for very large files (above a chosen threshold)

   * Define a threshold size where paging is enabled

10. Implement a paged buffer model:

    * Divide the file into fixed-size pages (e.g. 4 KiB, 16 KiB, etc)
    * Maintain a cache of pages currently in memory
    * Mark pages as clean or dirty

11. On reading bytes:

    * Determine which page(s) contain the requested range
    * Load missing pages from disk using POSIX I/O (pread/read)
    * Serve data from the cached pages

12. On modifying bytes:

    * Ensure the corresponding page is loaded
    * Update the page’s in-memory data
    * Mark page as dirty
    * Record edit operation for undo/redo

13. On insert/delete:

    * Adjust logical mapping from logical offsets to physical pages
    * Optionally use an internal structure (e.g. gap buffer, piece table, or segment list) to avoid physically shifting entire file data on each insert/delete

14. On save:

    * Iterate pages in logical order
    * Write them out to the temp file sequentially
    * Only write pages that are needed for the new logical file layout
    * Avoid holding the entire file in a single contiguous buffer

---

## 4. Safe save and backup mechanisms

15. Implement atomic save:

    * When user triggers Save:

      * Create a temporary file in the same directory as the original
      * Write the entire logical file contents to the temp file (using the paged model if needed)
      * Flush and fsync the temp file
      * Rename the temp file over the original file atomically

16. Preserve metadata:

    * Before saving, record:

      * Permissions (mode)
      * Ownership (uid, gid)
      * Timestamps (atime, mtime) if required
    * After writing temp file but before rename:

      * Apply original permissions and ownership to the temp file
      * Optionally restore timestamps

17. Optional backup:

    * If configured, create a backup copy (e.g. `original.bak`) before overwriting:

      * Copy or rename original file to backup name
      * Then proceed with atomic save over original name

18. Handle aborts and failures:

    * If writing to the temp file fails, keep original file untouched
    * Terminate save operation with error; remove partial temp file
    * Never partially overwrite original file

---

## 5. Rich UI features

19. Implement complete hex + ASCII view:

    * Address column showing offsets (e.g. 8 or 16 hex digits)
    * Hex column with bytes grouped (e.g. 16 bytes per line, with spacing)
    * ASCII column with printable characters, dots for non-printable

20. Implement cursor and navigation:

    * Maintain a current cursor position (logical offset + nibble position)
    * Support:

      * Arrow keys (left/right/up/down)
      * PageUp/PageDown
      * Home/End for line and file
    * Scroll view to keep cursor visible

21. Implement selection:

    * Support linear selection from anchor to current cursor
    * Optionally support extending selection with Shift + navigation keys
    * Render selection with highlight in both hex and ASCII columns

22. Implement editing:

    * Overwrite mode:

      * Two hex digits input change one byte at cursor
      * Advance cursor automatically
    * Insert mode:

      * Inserting new byte(s) shifts subsequent bytes forward using the model’s insert operation
    * Toggle between overwrite/insert mode via keybinding or UI control

23. Implement clipboard operations:

    * Copy selection as:

      * Raw bytes
      * Hex string representation (optional)
    * Paste:

      * Paste raw byte content at cursor using insert or overwrite, depending on mode

24. Implement search/replace:

    * Search:

      * For byte patterns (raw sequence)
      * For ASCII string (converted to bytes)
    * Replace:

      * Replace current or all occurrences with a specified byte sequence
    * Integrate search with paging (search across entire logical file)

25. Implement “Go to offset” dialog:

    * Accept numeric offset (decimal or hex)
    * Move cursor and view to that offset

26. Visual feedback:

    * Visually distinguish modified bytes from original (e.g. different color)
    * Highlight current line and current byte
    * Optionally show status bar info:

      * Current offset
      * Byte value in various bases
      * Insert/overwrite mode indicator

---

## 6. Metadata, special files, and safety policies

27. On opening a file, inspect file type via `stat`/`lstat`:

    * Regular file: allow editing
    * Symlink: either:

      * Resolve target and edit target file, or
      * Disallow editing and show a warning
    * Special files (device nodes, FIFOs, sockets): by default disallow editing and show warning

28. When saving:

    * Ensure new file retains:

      * Permissions and ownership
      * Correct SELinux/AppArmor labels if relevant (optional, advanced)
    * If metadata cannot be preserved, warn user and continue only if allowed

29. If user cancels:

    * Discard unsaved changes or prompt user to save/discard
    * Ensure original file is left untouched unless Save or Save As has completed successfully

---

## 7. Large file and edge-case testing

30. Test with very large files:

    * Files larger than physical RAM
    * Files larger than chosen page cache size
    * Ensure memory use remains bounded and UI remains responsive

31. Test with different file types:

    * Regular text files, binaries, images, executables, archives
    * Files with unusual permissions (read-only, root-owned, etc)

32. Test concurrent modifications:

    * Detect if file has changed on disk since it was opened or last saved (using timestamps or hashes)
    * On save, if external modification is detected, warn user and offer options:

      * Overwrite anyway
      * Save as new file
      * Reload and discard edits

33. Test editing scenarios:

    * Multiple insert/delete operations scattered throughout file
    * Deep undo/redo chains
    * Undo/redo across file size changes
    * Verify offset computations and selection ranges remain correct

---

## 8. Suggested development order (versions)

34. Version 1.0:

    * Minimal hex viewer/editor:

      * Overwrite only
      * Safe save (temp + rename)
      * Basic cursor navigation and display
    * No insert/delete, limited or no undo

35. Version 2.0:

    * Add undo/redo for overwrite operations
    * Add insert/delete support with dynamic model
    * Add improved UI: selection, copy/paste, go-to offset, basic search

36. Version 3.0:

    * Add paged model for large files
    * Add full search/replace across large files
    * Add configurable backups, robust metadata handling, and external-change detection
    * Optimize rendering and caching for responsiveness on multi-GB files
