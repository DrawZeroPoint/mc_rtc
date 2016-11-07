#!/bin/bash -ex

shopt -s expand_aliases

##########################
#  --  Configuration --  #
##########################

readonly mc_rtc_dir=`cd $(dirname $0)/..; pwd`

readonly SOURCE_DIR=`cd $mc_rtc_dir/../; pwd`
readonly INSTALL_PREFIX="/tmp"
readonly WITH_ROS_SUPPORT="true"
VREP_PATH=

readonly BUILD_TYPE="RelWithDebInfo"
if command -v nproc
then
  BUILD_CORE=`nproc`
else
  BUILD_CORE=`sysctl -n hw.ncpu`
fi
ROS_DISTRO=indigo
readonly ROS_APT_DEPENDENCIES="ros-${ROS_DISTRO}-common-msgs ros-${ROS_DISTRO}-tf2-ros ros-${ROS_DISTRO}-xacro ros-${ROS_DISTRO}-rviz-animated-view-controller"
ROS_GIT_DEPENDENCIES="git@gite.lirmm.fr:multi-contact/mc_ros#karim_drc git@gite.lirmm.fr:mc-hrp2/hrp2_drc#master git@gite.lirmm.fr:mc-hrp4/hrp4#master"
alias git_clone="git clone --quiet --recursive"
alias git_update="git pull && git submodule update"

SUDO_CMD=sudo
PIP_USER=
if [ -w $INSTALL_PREFIX ]
then
  SUDO_CMD=
  PIP_USER='--user'
fi

readonly gitlab_ci_yml=$mc_rtc_dir/.gitlab-ci.yml

export PATH=$INSTALL_PREFIX/bin:$PATH
export LD_LIBRARY_PATH=$INSTALL_PREFIX/lib:$LD_LIBRARY_PATH
export DYLD_LIBRARY_PATH=$INSTALL_PREFIX/lib:$DYLD_LIBRARY_PATH
export PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH
export PYTHONPATH=$INSTALL_PREFIX/lib/python2.7/site-packages:$PYTHONPATH

yaml_to_env()
{
  local var=$1
  local f=$2
  tmp=`grep "$var:" $f|sed -e"s/.*${var}: \"\(.*\)\"/\1/"`
  export $var="$tmp"
}

##############################
#  --  APT/Brew dependencies  --  #
##############################
KERN=$(uname -s)
if [ $KERN = Darwin ]
then
  export OS=Darwin
  # Install brew on the system
  if ! command -v brew
  then
    /usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
  fi
  brew tap homebrew/science
  brew update
  brew install coreutils gnu-sed wget python cmake doxygen jsoncpp qhull tinyxml2 geos boost eigen || true
  # eigen3.pc is broken in brew release
  gsed -i -e's@Cflags: -Iinclude/eigen3@Cflags: -I/usr/local/include/eigen3@' /usr/local/lib/pkgconfig/eigen3.pc
else
  export OS=$(lsb_release -si)
  if [ $OS = Ubuntu ]
  then
    yaml_to_env "APT_DEPENDENCIES" $gitlab_ci_yml
    APT_DEPENDENCIES=`echo $APT_DEPENDENCIES|sed -e's/libspacevecalg-dev//'|sed -e's/librbdyn-dev//'|sed -e's/libeigen-qld-dev//'|sed -e's/libsch-core-dev//'`
    sudo apt-get update
    sudo apt-get install -qq cmake build-essential gfortran doxygen libeigen3-dev python-pip ${APT_DEPENDENCIES}
  else
    echo "This script does not support your OS: ${OS}, please contact the maintainer"
    exit 1
  fi
fi

git_dependency_parsing()
{
  _input=$1
  git_dep=${_input%%#*}
  git_dep_branch=${_input##*#}
  if [ "$git_dep_branch" = "$git_dep" ]; then
    if [ -e "$2" ]; then
      git_dep_branch=$2
    else
      git_dep_branch="master"
    fi
  fi
  git_dep_uri_base=${git_dep%%:*}
  if [ "$git_dep_uri_base" = "$git_dep" ]; then
    git_dep_uri="git://github.com/$git_dep"
  else
    git_dep_uri=$git_dep
    git_dep=${git_dep##*:}
  fi
  git_dep=`basename $git_dep`
}

build_git_dependency()
{
  git_dependency_parsing $1
  echo "--> Compiling $git_dep (branch $git_dep_branch)"
  cd "$SOURCE_DIR"
  mkdir -p "$git_dep"
  if [ ! -d "$git_dep/.git" ]
  then
    git_clone -b $git_dep_branch "$git_dep_uri" "$git_dep"
  else
    pushd .
    cd "$git_dep"
    git_update
    popd
  fi
  mkdir -p $git_dep/build
  cd "$git_dep/build"
  cmake .. -DCMAKE_INSTALL_PREFIX:STRING="$INSTALL_PREFIX" \
           -DPYTHON_BINDING:BOOL=OFF \
           -DCMAKE_BUILD_TYPE:STRING="$BUILD_TYPE" \
           ${CMAKE_ADDITIONAL_OPTIONS}
  make -j${BUILD_CORE}
  ${SUDO_CMD} make install
}
###############################
##  --  GIT dependencies  --  #
###############################
yaml_to_env "GIT_DEPENDENCIES" $gitlab_ci_yml
# Add some source dependencies
GIT_DEPENDENCIES="jrl-umi3218/SpaceVecAlg jrl-umi3218/RBDyn jrl-umi3218/eigen-qld jrl-umi3218/sch-core ${GIT_DEPENDENCIES}"
for package in ${GIT_DEPENDENCIES}; do
  build_git_dependency "$package"
done

################################
#  -- Handle ROS packages  --  #
################################
if $WITH_ROS_SUPPORT
then
  if [ ! -e /opt/ros/${ROS_DISTRO}/setup.sh ]
  then
    if [ $OS = Ubuntu ]
    then
      sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu `lsb_release -c -s` main" > /etc/apt/sources.list.d/ros-latest.list'
      wget http://packages.ros.org/ros.key -O - | sudo apt-key add -
      sudo apt-get update
      sudo apt-get install -qq ros-${ROS_DISTRO}-ros-base ros-${ROS_DISTRO}-rosdoc-lite python-catkin-lint ${ROS_APT_DEPENDENCIES}
    else
      echo "Please install ROS and the required dependencies (${ROS_APT_DEPENDENCIES}) before continuing your installation or disable ROS support"
      exit 1
    fi
  fi
  . /opt/ros/${ROS_DISTRO}/setup.sh
  CATKIN_SRC_DIR=$SOURCE_DIR/catkin_ws/src
  mkdir -p $CATKIN_SRC_DIR
  cd $CATKIN_SRC_DIR
  catkin_init_workspace || true
  for package in ${ROS_GIT_DEPENDENCIES}; do
    git_dependency_parsing $package
    cd $CATKIN_SRC_DIR
    if [ ! -d "$git_dep/.git" ]
    then
      git_clone -b $git_dep_branch "$git_dep_uri" "$git_dep"
    else
      cd "$git_dep"
      git_update
    fi
  done
  cd $SOURCE_DIR/catkin_ws
  catkin_make
  . $SOURCE_DIR/catkin_ws/devel/setup.sh
else
  ROS_GIT_DEPENDENCIES=`echo $ROS_GIT_DEPENDENCIES|sed -e's/hrp4#master/hrp4#noxacro/'`
  for package in ${ROS_GIT_DEPENDENCIES}; do
    git_dependency_parsing $package
    cd $SOURCE_DIR
    if [ ! -d "$git_dep/.git" ]
    then
      git_clone -b $git_dep_branch "$git_dep_uri" "$git_dep"
    else
      cd "$git_dep"
      git_update
    fi
  done
fi

##########################
#  --  Build mc_rtc  --  #
##########################
cd $mc_rtc_dir
git submodule update --init
mkdir -p build
cd build
if $WITH_ROS_SUPPORT
then
  cmake ../ -DCMAKE_BUILD_TYPE:STRING="$BUILD_TYPE" \
            -DCMAKE_INSTALL_PREFIX:STRING="$INSTALL_PREFIX" \
            ${CMAKE_ADDITIONAL_OPTIONS}
else
  cmake ../ -DCMAKE_BUILD_TYPE:STRING="$BUILD_TYPE" \
            -DCMAKE_INSTALL_PREFIX:STRING="$INSTALL_PREFIX"\
            -DMC_ENV_DESCRIPTION_PATH:STRING="$SOURCE_DIR/mc_ros/mc_env_description"\
            -DHRP2_DRC_DESCRIPTION_PATH:STRING="$SOURCE_DIR/hrp2_drc/hrp2_drc_description"\
            -DHRP4_DESCRIPTION_PATH:STRING="$SOURCE_DIR/hrp4/hrp4_description" \
            ${CMAKE_ADDITIONAL_OPTIONS}
fi
make -j$BUILD_CORE
${SUDO_CMD} make install

#############################
#  --  Build mc_cython  --  #
#############################
cd $SOURCE_DIR
if [ ! -d mc_cython/.git ]
then
  git_clone git@gite.lirmm.fr:multi-contact/mc_cython
cd mc_cython
else
  cd mc_cython
  git_update
fi
${SUDO_CMD} pip install -r requirements.txt ${PIP_USER}
if [ ! -e eigen/eigen.pyx ]
then
  python generate_pyx.py
fi
make -j$BUILD_CORE
# Make sure the python prefix exists
mkdir -p ${INSTALL_PREFIX}/lib/python`python -c "import sys;print '{0}.{1}'.format(sys.version_info.major, sys.version_info.minor)"`/site-packages
${SUDO_CMD} make install

####################################################
#  -- Setup VREP, vrep-api-wrapper and mc_vrep --  #
####################################################
if [ "x${VREP_PATH}" = "x" ]
then
  cd $SOURCE_DIR
  if [ $OS = Darwin ]
  then
    if [ ! -d V-REP_PRO_EDU_V3_3_2_Mac ]
    then
      wget http://coppeliarobotics.com/V-REP_PRO_EDU_V3_3_2_Mac.zip
      unzip V-REP_PRO_EDU_V3_3_2_Mac.zip
    fi
    VREP_PATH=$SOURCE_DIR/V-REP_PRO_EDU_V3_3_2_Mac
  else
    VREP_VERSION=""
    if [ "`uname -i`" = "x86_64" ]
    then
      VREP_VERSION="_64"
    fi
    if [ ! -d V-REP_PRO_EDU_V3_3_2${VREP_VERSION}_Linux ]
    then
      wget http://coppeliarobotics.com/V-REP_PRO_EDU_V3_3_2${VREP_VERSION}_Linux.tar.gz
      tar xzf V-REP_PRO_EDU_V3_3_2${VREP_VERSION}_Linux.tar.gz
    fi
    VREP_PATH=$SOURCE_DIR/V-REP_PRO_EDU_V3_3_2${VREP_VERSION}_Linux
  fi
fi

cd $SOURCE_DIR
if [ ! -d vrep-api-wrapper/.git ]
then
  git_clone git@gite.lirmm.fr:vrep-utils/vrep-api-wrapper
  cd vrep-api-wrapper
else
  cd vrep-api-wrapper
  git_update
fi
mkdir -p build && cd build
cmake ../ -DCMAKE_BUILD_TYPE:STRING="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX:STRING="$INSTALL_PREFIX" \
          -DVREP_PATH:STRING="$VREP_PATH" \
          ${CMAKE_ADDITIONAL_OPTIONS}
make
${SUDO_CMD} make install

cd $SOURCE_DIR
if [ ! -d mc_vrep/.git ]
then
  git_clone git@gite.lirmm.fr:multi-contact/mc_vrep
  cd mc_vrep
else
  cd mc_vrep
  git_update
fi
mkdir -p build && cd build
cmake ../ -DCMAKE_BUILD_TYPE:STRING="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX:STRING="$INSTALL_PREFIX" \
          ${CMAKE_ADDITIONAL_OPTIONS}
make
${SUDO_CMD} make install

cd $SOURCE_DIR
if [ ! -d vrep_hrp/.git ]
then
  git_clone git@gite.lirmm.fr:mc-hrp4/vrep_hrp.git
fi

echo "Installation finished, please add the following lines to your .bashrc/.zshrc"
if [ ${OS} = Darwin ]
then
  echo """
  export PATH=$INSTALL_PREFIX/bin:\$PATH
  export DYLD_LIBRARY_PATH=$INSTALL_PREFIX/lib:\$DYLD_LIBRARY_PATH
  export PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH
  export PYTHONPATH=$INSTALL_PREFIX/lib/python2.7/site-packages:\$PYTHONPATH
  """
else
  echo """
  export PATH=$INSTALL_PREFIX/bin:\$PATH
  export LD_LIBRARY_PATH=$INSTALL_PREFIX/lib:\$LD_LIBRARY_PATH
  export PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH
  export PYTHONPATH=$INSTALL_PREFIX/lib/python2.7/site-packages:\$PYTHONPATH
  """
fi
