#import "VIMFlatReceipts.h"
#include <sqlite3.h>
#include <limits.h>

static const char *Schema =
    "PRAGMA user_version=17;"
    "CREATE TABLE acls (path_key INTEGER NOT NULL, pkg_key INTEGER NOT NULL, acl VARCHAR NOT NULL);"
    "CREATE TABLE groups (group_key INTEGER PRIMARY KEY AUTOINCREMENT, owner INTEGER NOT NULL, groupid VARCHAR NOT NULL);"
    "CREATE TABLE oldpkgs (pkg_key INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, owner INTEGER NOT NULL, pkgid VARCHAR NOT NULL, vers VARCHAR NOT NULL, ppath VARCHAR NOT NULL, replaces INTEGER, replacedby INTEGER);"
    "CREATE TABLE paths (path_key INTEGER PRIMARY KEY AUTOINCREMENT, path VARCHAR NOT NULL UNIQUE);"
    "CREATE TABLE pkgs (pkg_key INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER NOT NULL, owner INTEGER NOT NULL, pkgid VARCHAR NOT NULL, vers VARCHAR NOT NULL, ppath VARCHAR NOT NULL, replaces INTEGER);"
    "CREATE TABLE pkgs_groups (pkg_key INTEGER NOT NULL, group_key INTEGER NOT NULL);"
    "CREATE TABLE pkgs_paths (pkg_key INTEGER NOT NULL, path_key INTEGER NOT NULL, uid INTEGER, gid INTEGER, perms INTEGER);"
    "CREATE TABLE sha1s (path_key INTEGER NOT NULL, pkg_key INTEGER NOT NULL, sha1 BLOB NOT NULL);"
    "CREATE TABLE taints (pkg_key INTEGER NOT NULL, taint VARCHAR NOT NULL);"
    "CREATE INDEX paths_path_idx ON paths(path);"
    "CREATE INDEX pkgs_pkgid_idx ON pkgs(pkgid);"
    "CREATE INDEX pkgspaths_pathkey_idx ON pkgs_paths(path_key, pkg_key);"
    "CREATE INDEX pkgspaths_pkgkey_idx ON pkgs_paths(pkg_key, path_key);";

static void Check(sqlite3 *db,int result,int expected) {
    if(result!=expected)@throw [NSException exceptionWithName:@"HostInstall"
        reason:[NSString stringWithFormat:@"Could not write Leopard’s package receipts: %s (%d)",
            db?sqlite3_errmsg(db):sqlite3_errstr(result),db?sqlite3_extended_errcode(db):result]
        userInfo:nil];
}
static void Exec(sqlite3 *db,const char *sql) {Check(db,sqlite3_exec(db,sql,NULL,NULL,NULL),SQLITE_OK);}
static void Text(sqlite3 *db,sqlite3_stmt *s,int index,NSString *value) {
    Check(db,sqlite3_bind_text(s,index,value.UTF8String,-1,SQLITE_TRANSIENT),SQLITE_OK);
}
static void Integer(sqlite3 *db,sqlite3_stmt *s,int index,sqlite3_int64 value) {
    Check(db,sqlite3_bind_int64(s,index,value),SQLITE_OK);
}
static sqlite3_int64 Insert(sqlite3 *db,sqlite3_stmt *s) {
    Check(db,sqlite3_step(s),SQLITE_DONE);Check(db,sqlite3_reset(s),SQLITE_OK);
    return sqlite3_last_insert_rowid(db);
}
static sqlite3_int64 Key(sqlite3 *db,sqlite3_stmt *s,NSMutableDictionary *keys,NSString *name) {
    NSNumber *key=keys[name];
    if(!key) {
        Text(db,s,1,name);key=@(Insert(db,s));keys[name]=key;
    }
    return key.longLongValue;
}

static void RecordReceipts(NSURL *database,NSArray<NSDictionary *> *packages,
                           NSArray<NSString *> *aclPaths,NSString *acl,BOOL append) {
    sqlite3 *db=NULL;sqlite3_stmt *query[6]={0};
    @try {
        // DiskImages can mount at the /var spelling, but SQLite's NOFOLLOW
        // rejects even that system-owned /var -> /private/var alias.
        char parent[PATH_MAX],canonical[PATH_MAX];
        if(!realpath(database.URLByDeletingLastPathComponent.fileSystemRepresentation,parent))
            Check(NULL,SQLITE_CANTOPEN,SQLITE_OK);
        int written=snprintf(canonical,sizeof(canonical),"%s/%s",parent,database.lastPathComponent.fileSystemRepresentation);
        if(written<0||(size_t)written>=sizeof(canonical))Check(NULL,SQLITE_CANTOPEN,SQLITE_OK);
        int opened=sqlite3_open_v2(canonical,&db,
            SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOFOLLOW,NULL);
        Check(db,opened,SQLITE_OK);
        Exec(db,"BEGIN IMMEDIATE");
        if(!append)Exec(db,Schema);
        const char *sql[]={
            "INSERT INTO pkgs(timestamp,owner,pkgid,vers,ppath) VALUES (?,0,?,?,'./')",
            "INSERT INTO groups(owner,groupid) VALUES (0,?)",
            "INSERT INTO pkgs_groups VALUES (?,?)",
            "INSERT INTO paths(path) VALUES (?)",
            "INSERT INTO pkgs_paths VALUES (?,?,?,?,?)",
            "INSERT INTO acls VALUES (?,?,?)"
        };
        for(unsigned i=0;i<6;i++)Check(db,sqlite3_prepare_v2(db,sql[i],-1,&query[i],NULL),SQLITE_OK);
        NSMutableDictionary *groups=[NSMutableDictionary dictionary],*paths=[NSMutableDictionary dictionary];
        if(append) {
            for(NSDictionary *table in @[@{@"Query":@"SELECT group_key,groupid FROM groups WHERE owner=0",@"Keys":groups},
                                         @{@"Query":@"SELECT path_key,path FROM paths",@"Keys":paths}]) {
                sqlite3_stmt *existing=NULL;
                Check(db,sqlite3_prepare_v2(db,[table[@"Query"] UTF8String],-1,&existing,NULL),SQLITE_OK);
                @try {
                    int result;
                    while((result=sqlite3_step(existing))==SQLITE_ROW) {
                        NSString *name=@((const char *)sqlite3_column_text(existing,1));
                        table[@"Keys"][name]=@(sqlite3_column_int64(existing,0));
                    }
                    Check(db,result,SQLITE_DONE);
                } @finally {sqlite3_finalize(existing);}
            }
        }
        sqlite3_int64 timestamp=(sqlite3_int64)NSDate.date.timeIntervalSince1970;
        for(NSDictionary *package in packages) {
            NSString *identifier=package[@"Identifier"];
            Integer(db,query[0],1,timestamp);Text(db,query[0],2,identifier);Text(db,query[0],3,package[@"Version"]);
            sqlite3_int64 pkg=Insert(db,query[0]);
            for(NSString *group in package[@"Groups"]) {
                Integer(db,query[2],1,pkg);Integer(db,query[2],2,Key(db,query[1],groups,group));
                Insert(db,query[2]);
            }
            NSDictionary *metadata=package[@"Metadata"];
            for(NSString *path in [metadata.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
                if([path isEqual:@"."])continue;
                NSArray<NSNumber *> *values=metadata[path];
                Integer(db,query[4],1,pkg);Integer(db,query[4],2,Key(db,query[3],paths,path));
                for(int i=0;i<3;i++)Integer(db,query[4],3+i,values[i].longLongValue);
                Insert(db,query[4]);
            }
            if([identifier isEqual:@"com.apple.pkg.ACL"])for(NSString *path in aclPaths) {
                Integer(db,query[5],1,Key(db,query[3],paths,path));Integer(db,query[5],2,pkg);Text(db,query[5],3,acl);
                Insert(db,query[5]);
            }
        }
        Exec(db,"COMMIT");
    } @finally {
        for(unsigned i=0;i<6;i++)sqlite3_finalize(query[i]);
        if(db)sqlite3_close(db);
    }
}

void VIMWriteLeopardReceipts(NSURL *database,NSArray<NSDictionary *> *packages,
                            NSArray<NSString *> *aclPaths,NSString *acl) {
    RecordReceipts(database,packages,aclPaths,acl,NO);
}
void VIMAppendLeopardReceipts(NSURL *database,NSArray<NSDictionary *> *packages) {
    RecordReceipts(database,packages,@[],@"",YES);
}
