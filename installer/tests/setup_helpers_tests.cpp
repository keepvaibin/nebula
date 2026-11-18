// Nebula Setup's file helpers (installer/native/common.cpp, archive.cpp), P-121: a tree that cannot be deleted leaves no
// handle open; paths past MAX_PATH work for every file call (the program has no long-path manifest, as Setup has
// none); a release signature is checked over the file's SHA-256, read in pieces.
#include "archive.h"
#include "common.h"

#include <bcrypt.h>

#include <cstdio>
#include <string>

using namespace nebula;

namespace {

bool g_ok = true;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        g_ok = false;
    }
}

DWORD handle_count() {
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

// A failing delete (a file in a subfolder held open without delete sharing) throws, and leaves no handle behind.
void held_tree(const wstring& base) {
    const wstring root = PathCombine(base, L"held");
    const wstring deep = PathCombine(root, L"a", L"b");
    CreateDirs(deep);
    const wstring file = PathCombine(deep, L"held.txt");
    WriteAllText(file, L"held");
    HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    expect(held != INVALID_HANDLE_VALUE, "the file opens");
    auto attempt = [&] {
        try {
            DeleteTree(root);
            return false;
        } catch (const Error&) {
            return true;
        }
    };
    expect(attempt(), "a held tree is not deleted");  // the first: whatever the error text loads stays loaded
    const DWORD before = handle_count();
    expect(attempt(), "a held tree is not deleted, again");
    const DWORD after = handle_count();
    std::printf("handles before %lu, after %lu\n", before, after);
    expect(after == before, "a failed delete leaves no handle open");
    CloseHandle(held);
    DeleteTree(root);
    expect(!DirExists(root), "let go, the tree is deleted");
}

// Folders and files past MAX_PATH (260): written, listed, hashed, zipped, read back and extracted.
void long_paths(const wstring& base) {
    wstring deep = PathCombine(base, L"long");
    while (deep.size() < 300) deep = PathCombine(deep, L"a-folder-name-of-forty-characters-long-x");
    CreateDirs(deep);
    const wstring file = PathCombine(deep, L"abc.txt");
    WriteAllText(file, L"abc");
    expect(FileExists(file), "a file past MAX_PATH is written");
    expect(Sha256File(file) == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "its SHA-256");
    expect(FinalPath(file).size() > MAX_PATH, "its final path");
    const wstring zip = PathCombine(deep, L"test.zip");
    {
        ZipWriter writer(zip);
        writer.AddFile("abc.txt", file);
        writer.Finish();
    }
    auto reader = ZipReader::OpenFile(zip);
    const ZipReader::Entry* entry = reader->Find("abc.txt");
    expect(entry != nullptr, "the zip past MAX_PATH reads back");
    if (entry != nullptr) {
        const wstring out = PathCombine(deep, L"out", L"abc.txt");
        reader->ExtractTo(*entry, out, false);
        expect(ReadAllText(out) == L"abc", "an entry extracts past MAX_PATH");
    }
    reader.reset();
    expect(ListFiles(PathCombine(base, L"long"), true).size() == 3, "the files past MAX_PATH are listed");
    DeleteTree(PathCombine(base, L"long"));
    expect(!DirExists(PathCombine(base, L"long")), "the tree past MAX_PATH is deleted");
}

// A key made here, a signature over a file's SHA-256, checked against the key as XML (the release key's form).
void signature(const wstring& base) {
    const wstring file = PathCombine(base, L"signed.bin");
    std::string content(3u << 20, 'n');  // three of the hash's one-megabyte reads
    content += "the end";
    WriteAllBytes(file, content.data(), content.size());
    BCRYPT_ALG_HANDLE rsa = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    expect(BCryptOpenAlgorithmProvider(&rsa, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0, "RSA");
    expect(BCryptGenerateKeyPair(rsa, &key, 2048, 0) >= 0 && BCryptFinalizeKeyPair(key, 0) >= 0, "a key");
    ULONG size = 0;
    BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0);
    Bytes blob(size);
    BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), size, &size, 0);
    const auto* header = reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(blob.data());
    const uint8_t* exponent = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
    const uint8_t* modulus = exponent + header->cbPublicExp;
    const std::string xml = "<RSAKeyValue><Modulus>" + Base64Encode(modulus, header->cbModulus) + "</Modulus><Exponent>" +
                            Base64Encode(exponent, header->cbPublicExp) + "</Exponent></RSAKeyValue>";
    Sha256 sha;
    sha.Update(content.data(), content.size());
    std::vector<uint8_t> digest = sha.Finish();
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    BCryptSignHash(key, &padding, digest.data(), ULONG(digest.size()), nullptr, 0, &size, BCRYPT_PAD_PKCS1);
    Bytes signed_hash(size);
    BCryptSignHash(key, &padding, digest.data(), ULONG(digest.size()), signed_hash.data(), size, &size, BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(rsa, 0);
    const wstring wide_xml = Widen(xml);
    expect(VerifyRsaSha256File(file, signed_hash, wide_xml), "the signature holds");
    content.back() = '?';
    WriteAllBytes(file, content.data(), content.size());
    expect(!VerifyRsaSha256File(file, signed_hash, wide_xml), "a changed file fails it");
}

}  // namespace

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const wstring base = PathCombine(temp, L"nebula-setup-helpers-" + Num(GetCurrentProcessId()));
    try {
        CreateDirs(base);
        held_tree(base);
        long_paths(base);
        signature(base);
        DeleteTree(base);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %ls\n", ErrorText(error).c_str());
        g_ok = false;
    }
    std::printf("%s\n", g_ok ? "setup helpers: every case holds" : "setup helpers: FAILED");
    return g_ok ? 0 : 1;
}
