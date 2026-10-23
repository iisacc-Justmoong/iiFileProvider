#include <iiFileProviderDatabase.h>
#include <iiFileProviderNative.h>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace iiFileProvider::native::sql;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(int argc, char** argv) {
    CHECK(argc==2);
    iiFileProvider::native::TemporaryDirectory dir{std::filesystem::path(argv[1])};
    CHECK(dir.isValid());
    const auto path=dir.path()/std::filesystem::path(u8"한글.sqlite");
    CHECK(!Database::open(path));
    Options options; options.mode=OpenMode::create;
    auto db=Database::open(path,options); CHECK(db);
    CHECK(db->execute("CREATE TABLE data (id INTEGER PRIMARY KEY, text TEXT UNIQUE, data BLOB, real REAL, empty)"));
    auto insert=db->prepare("INSERT INTO data VALUES (?1,?2,?3,?4,?5)"); CHECK(insert);
    CHECK(insert->bind(1,std::int64_t{1}));
    CHECK(insert->bind(2,std::string("가\0나",7)));
    CHECK(insert->bind(3,Blob{0,1,2,255})); CHECK(insert->bind(4,1.25)); CHECK(insert->bind(5,{}));
    CHECK(!insert->bind(6,{})); CHECK(!insert->bind(4,std::numeric_limits<double>::infinity()));
    CHECK(insert->step()==Step::done); CHECK(insert->step()==Step::done); CHECK(db->changes()==1);
    CHECK(insert->reset()); CHECK(insert->bind(1,std::int64_t{1}));
    auto duplicate=insert->step(); CHECK(!duplicate && duplicate.error().code==Code::constraint);
    CHECK(!insert->step()); CHECK(insert->reset());
    auto rows=db->prepare("SELECT * FROM data"); CHECK(rows && rows->columns()==5);
    CHECK(!rows->value(0)); CHECK(rows->column_name(1)=="text"); CHECK(!rows->column_name(5));
    CHECK(rows->step()==Step::row);
    CHECK(std::get<std::string>(*rows->value(1))==std::string("가\0나",7));
    CHECK((std::get<Blob>(*rows->value(2))==Blob{0,1,2,255}));
    CHECK(std::get<double>(*rows->value(3))==1.25);
    CHECK(std::holds_alternative<std::monostate>(*rows->value(4)));
    CHECK(rows->step()==Step::done); CHECK(!rows->value(0));
    { auto tx=Transaction::begin(*db); CHECK(tx && db->in_transaction());
      CHECK(!Transaction::begin(*db)); CHECK(db->execute("INSERT INTO data(id) VALUES (2)")); }
    CHECK(!db->in_transaction());
    auto count=db->prepare("SELECT count(*) FROM data"); CHECK(count && count->step()==Step::row);
    CHECK(std::get<std::int64_t>(*count->value(0))==1); CHECK(count->step()==Step::done);
    { auto tx=Transaction::begin(*db); CHECK(tx); CHECK(db->execute("INSERT INTO data(id) VALUES (2)")); CHECK(tx->commit()); CHECK(!tx->commit()); }
    CHECK(!db->prepare("SELECT 1; DELETE FROM data"));
    CHECK(!db->prepare(std::string("SELECT 1\0;",10))); CHECK(!db->prepare("-- only a comment"));
    CHECK(db->prepare("SELECT 1; /* a trailing comment */"));
    Options ro; ro.mode=OpenMode::read_only; auto reader=Database::open(path,ro); CHECK(reader);
    CHECK(!reader->execute("DELETE FROM data"));
    Options small; small.maximum_value_bytes=1024;
    auto bounded=Database::memory(small); CHECK(bounded);
    auto large=bounded->prepare("SELECT ?"); CHECK(large && !large->bind(1,std::string(1025,'x')));
    auto retained=[] { auto owner=Database::memory(); CHECK(owner); return owner->prepare("SELECT 42"); }();
    CHECK(retained && retained->step()==Step::row && std::get<std::int64_t>(*retained->value(0))==42);
    Database closed; CHECK(!closed.prepare("SELECT 1"));
    { auto transaction=Transaction::begin(*db); CHECK(transaction);
      Options busy; busy.busy_timeout_ms=5; auto second=Database::open(path,busy); CHECK(second);
      auto locked=second->execute("INSERT INTO data(id) VALUES (3)"); CHECK(!locked && locked.error().code==Code::busy);
      CHECK(transaction->rollback()); }
    auto emptyBlob=db->prepare("SELECT ?1,?2"); CHECK(emptyBlob);
    CHECK(emptyBlob->bind(1,Blob{})); CHECK(emptyBlob->bind(2,{})); CHECK(emptyBlob->step()==Step::row);
    CHECK(std::holds_alternative<Blob>(*emptyBlob->value(0)) && std::holds_alternative<std::monostate>(*emptyBlob->value(1)));
    auto old=Transaction::begin(*db); CHECK(old); CHECK(db->execute("COMMIT"));
    auto fresh=Transaction::begin(*db); CHECK(fresh); CHECK(!old->rollback()); CHECK(db->in_transaction()); CHECK(fresh->rollback());
    CHECK(db->configure_durable()); CHECK(db->has_moved()==false);
    const Blob replacement{9,1,2,8};
    CHECK(!db->patch_blob("data","data",1,replacement));
    {
        auto tx=Transaction::begin(*db); CHECK(tx);
        auto patched=db->patch_blob("data","data",1,replacement); CHECK(patched && *patched==2);
        CHECK(db->patch_blob("data","data",1,replacement)==0);
        CHECK(!db->patch_blob("data","data",1,Blob{1}));
        CHECK(tx->rollback());
    }
    {
        auto row=db->prepare("SELECT data FROM data WHERE id=1"); CHECK(row && row->step()==Step::row);
        CHECK((std::get<Blob>(*row->value(0))==Blob{0,1,2,255})); CHECK(row->step()==Step::done);
    }
    auto copy=Database::open(dir.path()/"copy.sqlite",options); CHECK(copy);
    CHECK(!db->backup_to(*copy,1)); CHECK(!db->backup_to(*db,1024*1024));
    CHECK(db->backup_to(*copy,1024*1024));
    auto copied=copy->prepare("SELECT count(*) FROM data"); CHECK(copied && copied->step()==Step::row);
    CHECK(std::get<std::int64_t>(*copied->value(0))==2); CHECK(copied->step()==Step::done);
    std::cout << "native database contracts passed\n";
}
