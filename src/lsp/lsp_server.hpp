#ifndef TULPAR_LSP_SERVER_HPP
#define TULPAR_LSP_SERVER_HPP

namespace tulpar {

// Run the Tulpar Language Server Protocol loop on stdio. Blocks until the
// client sends `exit`. Returns the process exit code (0 on graceful
// shutdown, non-zero on protocol error).
int lsp_run_server();

}  // namespace tulpar

#endif  // TULPAR_LSP_SERVER_HPP
