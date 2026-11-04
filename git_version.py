# PlatformIO extra script: git revision and build time for src/version.cpp
#
# The defines are only added to version.cpp (build middleware), so on each build only
# that one file is recompiled, not the whole project.
import datetime
import subprocess

Import("env")


def git_revision():
    result = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"],
                            cwd=env["PROJECT_DIR"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unknown"


GIT_REVISION = git_revision()
BUILD_TIME = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
print("PagerCall: git %s, build %s" % (GIT_REVISION, BUILD_TIME))


def add_version_defines(env, node):
    if not node.get_path().endswith("version.cpp"):
        return node
    return env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + [
        ("PAGERCALL_GIT_REV", env.StringifyMacro(GIT_REVISION)),
        ("PAGERCALL_BUILD_TIME", env.StringifyMacro(BUILD_TIME)),
    ])


env.AddBuildMiddleware(add_version_defines)
