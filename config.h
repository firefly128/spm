/*
 * config.h - Configuration management for solpkg
 *
 * Reads/writes the master repo config from /opt/solpkg/etc/repos.conf
 * and manages SSL settings, repository definitions, and paths.
 */

#ifndef SOLPKG_CONFIG_H
#define SOLPKG_CONFIG_H

#define SOLPKG_VERSION   "0.1.0"
#define SOLPKG_BASE      "/opt/solpkg"
#define SOLPKG_ETC       SOLPKG_BASE "/etc"
#define SOLPKG_VAR       SOLPKG_BASE "/var"
#define SOLPKG_CACHE     SOLPKG_VAR  "/cache"
#define SOLPKG_ROLLBACK  SOLPKG_VAR  "/rollback"
#define SOLPKG_CONF      SOLPKG_ETC  "/repos.conf"
#define SOLPKG_INSTALLED SOLPKG_VAR  "/installed.db"
#define SOLPKG_IDX_DIR   SOLPKG_VAR  "/index"
#define SOLPKG_AGENT_PID SOLPKG_VAR  "/agent.pid"
#define SOLPKG_AGENT_LOG SOLPKG_VAR  "/agent.log"
#define SOLPKG_UPDATE_STATUS SOLPKG_VAR "/update.status"

#define SOLPKG_CA_BUNDLE "/usr/tgcware/etc/curl-ca-bundle.pem"

#define REPO_TYPE_TGCWARE   1
#define REPO_TYPE_GITHUB    2

#define MAX_REPOS           16
#define MAX_GH_REPOS        32

/* Repository definition */
typedef struct {
    char name[64];            /* Short ID: "tgcware", "firefly128" */
    char display_name[128];   /* Human name: "TGCware Solaris 7 SPARC" */
    int  type;                /* REPO_TYPE_TGCWARE or REPO_TYPE_GITHUB */
    int  enabled;             /* 1 = enabled, 0 = disabled */

    /* TGCware specific */
    char url[512];            /* Base URL for package index */
    char source_url[256];     /* GitHub URL for TGCware source */

    /* GitHub specific */
    char owner[64];           /* GitHub username: "firefly128" */
    char repos[MAX_GH_REPOS][64]; /* List of repo names */
    int  repo_count;
} repo_def_t;

/* Agent settings */
#define AGENT_DEFAULT_INTERVAL  (6 * 3600)   /* 6 hours */

/* Global config */
typedef struct {
    char ca_bundle[512];      /* Path to CA cert bundle for SSL */

    int  agent_interval;      /* Update check interval in seconds */
    int  agent_notify;        /* 1 = show CDE notification on updates */

    repo_def_t repos[MAX_REPOS];
    int repo_count;
} solpkg_config_t;

/*
 * Load config from SOLPKG_CONF.
 * Returns 0 on success, -1 on error (creates defaults if missing).
 */
int config_load(solpkg_config_t *cfg);

/*
 * Save config to SOLPKG_CONF.
 * Returns 0 on success, -1 on error.
 */
int config_save(const solpkg_config_t *cfg);

/*
 * Initialize default config (TGCware + firefly128 repos).
 */
void config_defaults(solpkg_config_t *cfg);

/*
 * Create required directories under SOLPKG_BASE.
 */
int config_init_dirs(void);

/*
 * Find a repo by name.
 * Returns pointer to the repo, or NULL.
 */
repo_def_t *config_find_repo(solpkg_config_t *cfg, const char *name);

#endif /* SOLPKG_CONFIG_H */
