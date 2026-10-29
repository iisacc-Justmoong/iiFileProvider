void files() {
  auto path = std::filesystem::current_path() / "modern-atomic-test.dat";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  const std::array<std::byte, 3> bytes{std::byte{1}, std::byte{2},
                                       std::byte{3}};
  auto file = iiFileProvider::core::AtomicFile::create(path);
  check(bool(file), "atomic create");
  auto temporary = file->temporary_path();
  check(bool(file->write(bytes)) && !std::filesystem::exists(path),
        "uncommitted target untouched");
  check(bool(file->commit()) && !std::filesystem::exists(temporary),
        "atomic commit retires temporary");
  auto replacement = iiFileProvider::core::AtomicFile::create(path);
  check(replacement && bool(replacement->write(std::span(bytes).first(1))),
        "replacement admission");
  temporary = replacement->temporary_path();
  check(bool(replacement->cancel()) && !std::filesystem::exists(temporary) &&
            std::filesystem::file_size(path) == 3,
        "cancel preserves original");
  auto again = iiFileProvider::core::AtomicFile::create(path);
  check(again && bool(again->write(std::span(bytes).first(1))) &&
            bool(again->commit()) && std::filesystem::file_size(path) == 1,
        "replace existing file");
  std::filesystem::remove(path, ec);
}