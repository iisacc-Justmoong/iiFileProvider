#include "ObjectStore.h"
#include "Authorship.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iiFileProvider;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected,"invalid metadata must be rejected");}
int main(){try{
    QTemporaryDir temporary(QStringLiteral(OBJECT_TEST_DIRECTORY "/author-package-XXXXXX"));check(temporary.isValid(),"fixture");
    const auto root=std::filesystem::path(temporary.path().toStdString());
    {std::ofstream out(root/"source");out<<"authored file";}
    const auto at=QDateTime::fromString("2026-09-28T01:00:00Z",Qt::ISODate);
    auto creator=FileAuthor::fromIisaccAccount({{"sub","creator"},{"email","creator@example.com"},{"displayName","원작자"}},QUrl("https://iisacc.com"),at).value();
    auto metadata=creator.metadata();metadata.details.biography="Biography\nSecond line";metadata.details.organization="Studio";
    metadata.attribution.roles={"creator"};check(creator.setMetadata(metadata),"full author metadata");
    auto editor=FileAuthor::fromIisaccAccount({{"sub","editor"},{"email","editor@example.com"}},QUrl("https://iisacc.com"),at).value();
    Authorship authors;authors.setAuthor(creator,at);authors.setAuthor(editor,at.addSecs(1));
    check(authors.setLinksFromStrings({"[Original|society:original]"},at.addSecs(2)),"file links");
    const auto serialized=authors.dump().toStdString();
    ObjectAuthor importer{"local:society","packager","Packaging agent"};
    ObjectStore store(root/"package","author-container",true);
    const auto session=store.beginSession(importer,"mac","migration");
    ObjectWriteOptions options;options.authorship=ObjectMetadata{"iiFileProvider.Authorship/3",serialized};options.sourceStamp="original-stamp";
    auto record=store.importFile(root/"source","Files/authored",session.key,options);
    check(record.author.subject=="packager" && record.authorship.payload==serialized,"actor separate from original attribution");
    auto restored=Authorship::fromDump(QByteArray::fromStdString(record.authorship.payload));check(bool(restored),"restore full authorship");
    check(restored->toJson()==authors.toJson(),"all profile/contribution/link fields preserved");
    check(restored->firstEditor()->metadata().account.sub=="creator" && restored->participants().size()==1,"original roster preserved");
    auto next=store.reviseFile(record.key,root/"source",1,session.key);
    check(next.authorship.payload==serialized && next.sourceStamp.empty() && store.validate(next.key,2),"revision preserves authorship and invalidates old source stamp");
    const auto unknown=store.importFile(root/"source","Files/unknown",session.key);
    check(unknown.authorship.payload.empty(),"unknown original creator must not become importer");
    ObjectAuthor fullActor{"https://iisacc.com","creator","원작자",{"iiFileProvider.FileAuthor/1",QJsonDocument(creator.toJson()).toJson(QJsonDocument::Compact).toStdString()}};
    const auto detailed=store.beginSession(fullActor,"device","edit");
    auto changed=store.reviseFile(record.key,root/"source",2,detailed.key);
    check(changed.author.profile==fullActor.profile && store.validate(changed.key,3),"full actor profile and validation");
    ObjectWriteOptions invalid;invalid.authorship=ObjectMetadata{"iiFileProvider.Authorship/3","not-json"};
    rejects([&]{store.importFile(root/"source","Files/invalid",session.key,invalid);});
    invalid.authorship=ObjectMetadata{"iiFileProvider.Authorship/3",R"({"schemaVersion":3,"token":"must-not-persist"})"};
    rejects([&]{store.importFile(root/"source","Files/credential",session.key,invalid);});
    invalid.authorship=fullActor.profile;
    rejects([&]{store.importFile(root/"source","Files/profile-is-not-roster",session.key,invalid);});
    rejects([&]{store.reviseFile(record.key,root/"source",3,session.key,invalid);});
    check(store.lookup(record.key)->version==3,"invalid roster leaves head unchanged");
    std::stop_source cancellation;cancellation.request_stop();ObjectWriteOptions cancelled;cancelled.cancellation=cancellation.get_token();
    rejects([&]{store.importFile(root/"source","Files/cancelled",session.key,cancelled);});
    check(!store.lookupPath("Files/cancelled"),"cancelled transaction leaves no object");
    std::cout<<"Lossless authorship and actor separation passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
