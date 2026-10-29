#pragma once
namespace iiFileProvider::detail {
inline constexpr const char *ontologySchema = R"SQL(
CREATE TABLE IF NOT EXISTS ontology_state(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL);
INSERT OR IGNORE INTO ontology_state VALUES(1,0);
CREATE TABLE IF NOT EXISTS ontology_classes(key TEXT PRIMARY KEY,label TEXT NOT NULL,description TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS ontology_parents(child TEXT NOT NULL REFERENCES ontology_classes(key),parent TEXT NOT NULL REFERENCES ontology_classes(key),PRIMARY KEY(child,parent));
CREATE INDEX IF NOT EXISTS ontology_parents_reverse ON ontology_parents(parent,child);
CREATE TABLE IF NOT EXISTS ontology_properties(key TEXT PRIMARY KEY,document TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS ontology_nodes(key TEXT PRIMARY KEY,object_key TEXT UNIQUE REFERENCES objects(object_key),document TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS ontology_types(resource_key TEXT NOT NULL REFERENCES ontology_nodes(key),class_key TEXT NOT NULL REFERENCES ontology_classes(key),PRIMARY KEY(resource_key,class_key));
CREATE INDEX IF NOT EXISTS ontology_types_class ON ontology_types(class_key,resource_key);
CREATE TABLE IF NOT EXISTS ontology_statements(sequence INTEGER PRIMARY KEY AUTOINCREMENT,subject TEXT NOT NULL REFERENCES ontology_nodes(key),predicate TEXT NOT NULL REFERENCES ontology_properties(key),kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 4),value TEXT NOT NULL,object_key TEXT REFERENCES ontology_nodes(key),UNIQUE(subject,predicate,kind,value));
CREATE INDEX IF NOT EXISTS ontology_statement_value ON ontology_statements(predicate,kind,value,subject);
CREATE INDEX IF NOT EXISTS ontology_statement_reverse ON ontology_statements(object_key,predicate,subject);
CREATE INDEX IF NOT EXISTS ontology_statement_subject ON ontology_statements(subject,sequence);
CREATE TABLE IF NOT EXISTS ontology_directories(path TEXT PRIMARY KEY,document TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS ontology_directory_properties(path TEXT NOT NULL REFERENCES ontology_directories(path) ON DELETE CASCADE,predicate TEXT NOT NULL REFERENCES ontology_properties(key),PRIMARY KEY(path,predicate));
CREATE INDEX IF NOT EXISTS ontology_directory_predicate ON ontology_directory_properties(predicate,path);
CREATE TABLE IF NOT EXISTS ontology_commits(revision INTEGER PRIMARY KEY,session_key TEXT NOT NULL REFERENCES sessions(session_key),at_ns INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS ontology_journal(sequence INTEGER PRIMARY KEY AUTOINCREMENT,revision INTEGER NOT NULL REFERENCES ontology_commits(revision) DEFERRABLE INITIALLY DEFERRED,operation TEXT NOT NULL,subject TEXT NOT NULL,before_json TEXT NOT NULL,after_json TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS ontology_journal_revision ON ontology_journal(revision,sequence);
CREATE INDEX IF NOT EXISTS ontology_commit_session ON ontology_commits(session_key,revision);
CREATE UNIQUE INDEX IF NOT EXISTS ontology_file_object ON current_index(object_key);
PRAGMA user_version=5;
)SQL";
}
