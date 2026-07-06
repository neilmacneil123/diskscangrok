#include "search/FileIndex.hpp"
#include "search/Search.hpp"

#include <cassert>
#include <memory>

using namespace diskscan;

static std::shared_ptr<FileNode> makeNode(const std::string& path,
                                          const std::string& name,
                                          uint64_t size,
                                          NodeType type) {
    auto node = std::make_shared<FileNode>();
    node->path = path;
    node->name = name;
    node->sizeBytes = size;
    node->type = type;
    return node;
}

int main() {
    auto root = makeNode("C:\\", "C:\\", 3000, NodeType::Directory);
    auto windows = makeNode("C:\\Windows", "Windows", 2000, NodeType::Directory);
    auto dll = makeNode("C:\\Windows\\kernel32.dll", "kernel32.dll", 1024, NodeType::File);
    auto zip = makeNode("C:\\backup.zip", "backup.zip", 512, NodeType::File);
    auto song = makeNode("C:\\Users\\neilmacneil\\song.mp3", "song.mp3", 2048, NodeType::File);
    auto photo = makeNode("C:\\Users\\neilmacneil\\pic.jpg", "pic.jpg", 256, NodeType::File);

    windows->parent = root.get();
    dll->parent = windows.get();
    zip->parent = root.get();
    song->parent = root.get();
    photo->parent = root.get();
    root->children = {windows, zip, song, photo};
    windows->children = {dll};

    FileIndex index;
    index.build(root);

    auto byName = Search::query(index, "kernel32");
    assert(byName.size() == 1);
    assert(byName[0]->name == "kernel32.dll");

    auto byExt = Search::query(index, "ext:dll");
    assert(byExt.size() == 1);

    auto byGlob = Search::query(index, "*.zip");
    assert(byGlob.size() == 1);
    assert(byGlob[0]->name == "backup.zip");

    auto bySize = Search::query(index, "size:>1kb");
    assert(bySize.size() == 1);
    assert(bySize[0]->name == "kernel32.dll");

    auto byPath = Search::query(index, "path:windows file:");
    assert(byPath.size() == 1);
    assert(byPath[0]->name == "kernel32.dll");

    auto musicForNeil = Search::query(index, "neilmacneil audio:");
    assert(musicForNeil.size() == 1);
    assert(musicForNeil[0]->name == "song.mp3");

    auto pictures = Search::query(index, "pictures:");
    assert(pictures.size() == 1);
    assert(pictures[0]->name == "pic.jpg");

    auto archives = Search::query(index, "compressed:");
    assert(archives.size() == 1);
    assert(archives[0]->name == "backup.zip");

    auto folders = Search::query(index, "folders:");
    assert(folders.size() == 2);

    return 0;
}