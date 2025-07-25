# AxonVex Framework Source Directory

# Module dependency order (topological sort):
# 1. Types (no dependencies)
# 2. Utils (depends on Types only) 
# 3. Core (depends on Utils, Types)
# 4. IO (depends on Core, Utils, Types)
# 5. Interfaces (depends on IO, Core, Utils, Types)
# 6. Plugins (depends on all above)

# This file is now just a coordinator - individual modules handle their own sources
# Each module adds its sources directly to the main axonvex library target