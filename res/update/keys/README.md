# System Update keys

The public halves of the keys a System Update list must be signed with
(docs/updates.md). plastron copies every `<key id>.pem` here into the system
image at `/etc/zyperos/update-keys/`, where the card cannot change them.

- `tools/update-key.sh new` makes the private key on the Mac that signs
  releases (never in a repository, a container or GitHub) and writes its
  public half here. Commit that file; the next image trusts it.
- An image built while this folder has no key has System Update switched off.
- To replace a key, add the new one here and keep signing with the old one
  until images with the new one are out; then remove the old file.
