-- Produced by the original schema-1 library before the metadata upgrade.
PRAGMA application_id=1397703242;
PRAGMA user_version=1;
BEGIN TRANSACTION;
CREATE TABLE store(container_key TEXT NOT NULL);
INSERT INTO store VALUES('legacy-container');
CREATE TABLE sessions(session_key TEXT PRIMARY KEY,origin TEXT NOT NULL,subject TEXT NOT NULL,name TEXT NOT NULL,device TEXT NOT NULL,description TEXT NOT NULL,started INTEGER NOT NULL,ended INTEGER NOT NULL);
INSERT INTO sessions VALUES('session-dff1745d7e5f718e9c961287d37714b5','https://iisacc.com','legacy-actor','Legacy','device','legacy fixture',1790577390343646000,1790577391033283000);
CREATE TABLE objects(index_key INTEGER PRIMARY KEY AUTOINCREMENT,object_key TEXT UNIQUE NOT NULL,head INTEGER NOT NULL,path TEXT NOT NULL,deleted INTEGER NOT NULL CHECK(deleted IN(0,1)));
INSERT INTO objects VALUES(1,'object-e5835c2023020615c4f2c74dfe9fac0d',1,'Files/legacy.txt',0);
CREATE TABLE revisions(object_key TEXT NOT NULL REFERENCES objects(object_key),version INTEGER NOT NULL CHECK(version>0),path TEXT NOT NULL,sha256 TEXT NOT NULL,size INTEGER NOT NULL CHECK(size>=0),author_origin TEXT NOT NULL,author_subject TEXT NOT NULL,author_name TEXT NOT NULL,session_key TEXT NOT NULL REFERENCES sessions(session_key),at_ns INTEGER NOT NULL,operation TEXT NOT NULL,deleted INTEGER NOT NULL CHECK(deleted IN(0,1)),parent_key TEXT NOT NULL,validation_key TEXT NOT NULL,PRIMARY KEY(object_key,version));
INSERT INTO revisions VALUES('object-e5835c2023020615c4f2c74dfe9fac0d',1,'Files/legacy.txt','c49fea7425fa7f8699897a97c159c6690267d9003bb78c53fafa8fc15c325d84',6,'https://iisacc.com','legacy-actor','Legacy','session-dff1745d7e5f718e9c961287d37714b5',1790577390801617000,'import',0,'','society-object-v1:c5e89d5a30a4230663fcd04b4228fe4fb37e5cfbfff53c1e4653b5febe746089');
CREATE TABLE chunks(sha256 TEXT PRIMARY KEY,data BLOB NOT NULL);
INSERT INTO chunks VALUES('c49fea7425fa7f8699897a97c159c6690267d9003bb78c53fafa8fc15c325d84',x'6c6567616379');
CREATE TABLE revision_chunks(object_key TEXT NOT NULL,version INTEGER NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal>=0),sha256 TEXT NOT NULL REFERENCES chunks(sha256),size INTEGER NOT NULL CHECK(size>0),PRIMARY KEY(object_key,version,ordinal),FOREIGN KEY(object_key,version) REFERENCES revisions(object_key,version));
INSERT INTO revision_chunks VALUES('object-e5835c2023020615c4f2c74dfe9fac0d',1,0,'c49fea7425fa7f8699897a97c159c6690267d9003bb78c53fafa8fc15c325d84',6);
CREATE UNIQUE INDEX live_paths ON objects(path) WHERE deleted=0;
CREATE INDEX live_index ON objects(deleted,index_key);
COMMIT;
