#
# Sets specific build flag for edge-impulse-sdk to avoid error on warning
# This script is called by platformio.ini
#

Import("env")     # Yes, starts with a capital letter. This is not a typo.
import sys

def edge_impulse_sdk_configuration(env, node):
    """
    `node.name` - a name of File System Node
    `node.get_path()` - a relative path
    `node.get_abspath()` - an absolute path
    """

    # Do not modify node if path does not contain "edge-impulse-sdk"
    # Try to narrow scope as much as possible!
    if "edge-impulse-sdk" not in node.path:
        return node

    #print(env.Dump())

    # Remove some build flags
    print("WARNING: Modifying flags for", node.name)
    env.Append(CCFLAGS=["-Wno-error=maybe-uninitialized"])
    return node

env.AddBuildMiddleware(edge_impulse_sdk_configuration)