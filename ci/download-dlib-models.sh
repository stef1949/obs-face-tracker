#!/usr/bin/env bash

set -euo pipefail

flg_nonfree=0

while (($# > 0)); do
	case "$1" in
		--nonfree)
			flg_nonfree=1
			shift
			;;
		*)
			echo "Error: unknown option $1" >&2
			exit 1
			;;
	esac
done

readonly DLIB_MODELS_REVISION="fd81b6308a6a73d4ce08859eb2f4b628a21e27a2"
readonly DLIB_MODELS_BASE="https://raw.githubusercontent.com/davisking/dlib-models/${DLIB_MODELS_REVISION}"
readonly OPENCV_ZOO_REVISION="47534e27c9851bb1128ccc0102f1145e27f23f98"
readonly OPENCV_ZOO_BASE="https://raw.githubusercontent.com/opencv/opencv_zoo/${OPENCV_ZOO_REVISION}/models/face_detection_yunet"
readonly OPENCV_ZOO_MEDIA="https://media.githubusercontent.com/media/opencv/opencv_zoo/${OPENCV_ZOO_REVISION}/models/face_detection_yunet"
readonly HOG_MODEL_URL="https://github.com/norihiro/obs-face-tracker/releases/download/0.7.0-hogdata/frontal_face_detector.dat.bz2"
readonly DATA_DIR="${DESTDIR:-}data"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/obs-face-tracker-models.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

download_and_verify()
{
	local url=$1
	local output=$2
	local sha256=$3
	curl --fail --location --retry 3 --silent --show-error "$url" --output "$output"
	verify_sha256 "$output" "$sha256"
}

verify_sha256()
{
	local file=$1
	local expected=$2
	local actual
	if command -v sha256sum >/dev/null 2>&1; then
		actual=$(sha256sum "$file")
	elif command -v shasum >/dev/null 2>&1; then
		actual=$(shasum -a 256 "$file")
	else
		echo "Error: sha256sum or shasum is required" >&2
		exit 1
	fi
	actual=${actual%% *}
	if [[ "$actual" != "$expected" ]]; then
		echo "Error: SHA-256 mismatch for $file" >&2
		echo "Expected: $expected" >&2
		echo "Actual:   $actual" >&2
		exit 1
	fi
}

extract_and_verify()
{
	local archive=$1
	local output=$2
	local sha256=$3
	bunzip2 --stdout "$archive" > "$output"
	verify_sha256 "$output" "$sha256"
}

mkdir -p "$DATA_DIR/dlib_hog_model" "$DATA_DIR/dlib_cnn_model" "$DATA_DIR/dlib_face_landmark_model" \
	"$DATA_DIR/yunet_model"

download_and_verify "$HOG_MODEL_URL" "$work_dir/frontal_face_detector.dat.bz2" \
	"7ef0315b8a893808a8c8c1677c7f49b519538ed1b4205d6d240a0274effd2975"
extract_and_verify "$work_dir/frontal_face_detector.dat.bz2" \
	"$DATA_DIR/dlib_hog_model/frontal_face_detector.dat" \
	"f3f7aa833fb4a14a46fc48689f45b98d85ef72d6dfaf8bdc012b7d3c522a5426"

download_and_verify "$DLIB_MODELS_BASE/mmod_human_face_detector.dat.bz2" \
	"$work_dir/mmod_human_face_detector.dat.bz2" \
	"db9e9e40f092c118d5eb3e643935b216838170793559515541c56a2b50d9fc84"
extract_and_verify "$work_dir/mmod_human_face_detector.dat.bz2" \
	"$DATA_DIR/dlib_cnn_model/mmod_human_face_detector.dat" \
	"be467b1a76f482693de3b0f6a1ff91d092319be71523d4d4b0628f6a53fcb87a"

download_and_verify "$DLIB_MODELS_BASE/shape_predictor_5_face_landmarks.dat.bz2" \
	"$work_dir/shape_predictor_5_face_landmarks.dat.bz2" \
	"6e787bbebf5c9efdb793f6cd1f023230c4413306605f24f299f12869f95aa472"
extract_and_verify "$work_dir/shape_predictor_5_face_landmarks.dat.bz2" \
	"$DATA_DIR/dlib_face_landmark_model/shape_predictor_5_face_landmarks.dat" \
	"c4b1e9804792707d3a405c2c16a80a20269e6675021f64a41d30fffafbc41888"

download_and_verify "$DLIB_MODELS_BASE/LICENSE" "$DATA_DIR/LICENSE-dlib-models" \
	"6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01"

download_and_verify "$OPENCV_ZOO_MEDIA/face_detection_yunet_2026may.onnx" \
	"$DATA_DIR/yunet_model/face_detection_yunet_2026may.onnx" \
	"ebafce4e3c118d6554634be5c27ab333b4c047a9a8c3faf1d7cf93101c22f0f0"
download_and_verify "$OPENCV_ZOO_BASE/LICENSE" "$DATA_DIR/LICENSE-yunet" \
	"c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958"

cp dlib/LICENSE.txt "$DATA_DIR/LICENSE-dlib"
cp ci/dlib-models-manifest.txt "$DATA_DIR/MODEL-MANIFEST.txt"

if ((flg_nonfree)); then
	download_and_verify "$DLIB_MODELS_BASE/shape_predictor_68_face_landmarks.dat.bz2" \
		"$work_dir/shape_predictor_68_face_landmarks.dat.bz2" \
		"7d6637b8f34ddb0c1363e09a4628acb34314019ec3566fd66b80c04dda6980f5"
	extract_and_verify "$work_dir/shape_predictor_68_face_landmarks.dat.bz2" \
		"$DATA_DIR/dlib_face_landmark_model/shape_predictor_68_face_landmarks.dat" \
		"fbdc2cb80eb9aa7a758672cbfdda32ba6300efe9b6e6c7a299ff7e736b11b92f"

	download_and_verify "$DLIB_MODELS_BASE/shape_predictor_68_face_landmarks_GTX.dat.bz2" \
		"$work_dir/shape_predictor_68_face_landmarks_GTX.dat.bz2" \
		"fbe6b48bc196aab4164167c60ee1e0b54f527e80865beff5778b3c7464bcc639"
	extract_and_verify "$work_dir/shape_predictor_68_face_landmarks_GTX.dat.bz2" \
		"$DATA_DIR/dlib_face_landmark_model/shape_predictor_68_face_landmarks_GTX.dat" \
		"249a69a1d5f2d7c714a92934d35367d46eb52dc308d46717e82d49e8386b3b80"

	download_and_verify "$DLIB_MODELS_BASE/README.md" "$work_dir/dlib-models-README.md" \
		"9c6d18a2a83ceebc136997941ec913ccc0548889e24440381bd06d7453b05e3c"
	awk '/^##/{p=0} /^##.*shape_predictor_68/{p=1} p' "$work_dir/dlib-models-README.md" \
		> "$DATA_DIR/LICENSE-shape_predictor_68_face_landmarks"
fi
