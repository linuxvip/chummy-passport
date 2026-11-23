"""Static checks for the profile-photo storage and authenticated HTTP contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
upload = (ROOT / "main/apps/home/home_upload.cc").read_text()
photo = (ROOT / "main/apps/home/home_photo.c").read_text()
header = (ROOT / "main/apps/home/home_photo.h").read_text()

assert '/api/profile-photo' in upload
for method in ("HTTP_GET", "HTTP_POST", "HTTP_DELETE"):
    assert method in upload
assert "handler_profile_photo_get" in upload
assert "handler_profile_photo_post" in upload
assert "handler_profile_photo_delete" in upload
assert upload.count('"/api/profile-photo"') >= 3
assert "if (!token_ok(req))" in upload
assert "config.max_uri_handlers = 13;" in upload
assert "content_len" in upload and "HOME_PROFILE_PHOTO_BYTES" in upload
assert "uint8_t chunk[512]" in upload

assert "#define HOME_PROFILE_PHOTO_BYTES" in header
assert "#define HOME_PROFILE_PHOTO_W 108" in header
assert "#define HOME_PROFILE_PHOTO_H 144" in header
assert "#define HOME_PROFILE_PHOTO_LEGACY_W 90" in header
assert "#define HOME_PROFILE_PHOTO_LEGACY_H 120" in header
assert "home_photo_read_range" in header
assert "home_photo_delete" in header
assert "profile-photo.tmp" in photo
assert "profile-photo.backup" in photo
assert "rename(source, kPath)" in photo
assert "info.st_size != (off_t)HOME_PROFILE_PHOTO_BYTES" not in photo
assert "file_has_size(kTmpPath, s_total_bytes)" in photo
assert "scale_legacy_upload_locked" in photo
assert "profile-photo.scaled" in photo

print("Profile photo API: authenticated GET/POST/DELETE, exact size and staged replacement PASS")
