# TODO

## Patch loading

- [ ] Add **Load with Patch** to the content picker. Ask for the base ROM and an
  IPS, BPS, or UPS patch.
- [ ] Let users save the pair as a versioned `.patched` recipe. Offer the patch
  folder as the default location. Store relative paths when practical and
  resolve them from the recipe's folder.
- [ ] Teach the content picker and Recent Games to reopen a recipe, apply its
  patch, and launch the resulting ROM. When a referenced file is missing, let
  the user locate it and update the recipe.
- [ ] Use the patch filename stem for both save RAM and savestates within the
  existing per-core folders. Regular ROMs use their filename stems, and users
  can rename files to distinguish titles with the same stem.

## Review follow-ups

- [x] **#2 Browser file uploads** — Keep the destination fixed for the entire
  upload batch and ask before replacing an existing file.
- [ ] **#3 Browser ROM imports** — Handle a filename collision before launch so
  opening a newly selected ROM cannot launch a previously imported file.
- [ ] **#4 Guest-card uploads** — Make upload retries cancellable at shutdown
  and let a ready guest link be shared while its card is uploading.
- [ ] **#5 Core catalog selection** — Preserve the selected core by filename
  across refreshes, and clear the selection if that core disappears.
