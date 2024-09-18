Import("env")
import subprocess

def get_version_string():
    git_describe = subprocess.check_output(["git", "describe", "--tags", "--dirty", "--always"]).strip()
    print("Version: " + git_describe.decode('utf-8'))
    return git_describe.decode('utf-8')    

env.Append(CPPDEFINES=[("EUB_VERSION", get_version_string())])
