Import ("env")

env_name = env["PIOENV"]

# add variant dir from MeshCore tree in libdeps to includes
for item in env.get("BUILD_FLAGS", []):
    if "MC_VARIANT" in item :
        variant_name = item.split("=")[1]
        env.Append(BUILD_FLAGS=[f"-I .pio/libdeps/{env_name}/MeshCore/variants/{variant_name}"])

# add advert name from PIOENV
env.Append(BUILD_FLAGS=[f"-D ADVERT_NAME=\'\"{env_name}\"\'"])
