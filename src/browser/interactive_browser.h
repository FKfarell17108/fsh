#ifndef FSH_BROWSER_INTERACTIVE_BROWSER_H
#define FSH_BROWSER_INTERACTIVE_BROWSER_H

typedef enum { BROWSE_MODE_LS, BROWSE_MODE_DIR } BrowseMode;

typedef enum { BROWSER_RESULT_QUIT, BROWSER_RESULT_CD, BROWSER_RESULT_OPEN_FILE } BrowserResultKind;

typedef struct {
    BrowserResultKind kind;
    char path[4096];
    char editor[256];
} BrowserResult;

BrowserResult interactive_browser_run(BrowseMode mode);
BrowserResult interactive_ls(void);
BrowserResult interactive_dir(void);

#endif
