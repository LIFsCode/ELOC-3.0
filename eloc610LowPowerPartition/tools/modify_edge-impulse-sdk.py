#
# Sets specific build flag for edge-impulse-sdk to avoid error on warning
# This script is called by platformio.ini
#

Import("env")     # Yes, starts with a capital letter. This is not a typo.
import sys

# --- Add custom macros for the ALL files which path contains "edge-impulse-sdk"
def edge_impulse_sdk_configuration(env, node):
    """
    `node.name` - a name of File System Node
    `node.get_path()` - a relative path
    `node.get_abspath()` - an absolute path
    """

    # do not modify node if path does not contain "edge-impulse-sdk"
    # Try to limit scope of change as much as possible!
    if "edge-impulse-sdk" not in node.get_path():
        return node

    # Remove some build flags
    print("WARNING: Adding custom build flags for edge-impulse-sdk")
    env.ProcessUnFlags("-Werror=maybe-uninitialized -Wmissing-field-initializers")
    return node

env.AddBuildMiddleware(edge_impulse_sdk_configuration)