info(){ printf "\e[1;34m[INFO]\e[0m %s\n" "$*"; }

info "Cleaning..."
rm -r isodir
rm -r build
rm *.o