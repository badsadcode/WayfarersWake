// Port of Thornwake Game/WorldGen: WorldProfile.cs, WorldRecipe.cs, WorldLayout.cs (+ a small JSON reader).
#include "WwWorld.h"
#include <charconv>

namespace ww
{
// ------------------------------------------------------------------------------------------------ JSON
namespace
{
struct FJsonReader
{
	const std::string& T; size_t I = 0; std::string Error;
	explicit FJsonReader(const std::string& Text) : T(Text) {}

	void Skip()
	{
		for (;;)
		{
			while (I < T.size() && (T[I] == ' ' || T[I] == '\t' || T[I] == '\r' || T[I] == '\n')) I++;
			if (I + 1 < T.size() && T[I] == '/' && T[I + 1] == '/') { while (I < T.size() && T[I] != '\n') I++; continue; }
			if (I + 1 < T.size() && T[I] == '/' && T[I + 1] == '*') { I += 2; while (I + 1 < T.size() && !(T[I] == '*' && T[I + 1] == '/')) I++; I += 2; continue; }
			if (I + 2 < T.size() && (unsigned char)T[I] == 0xEF && (unsigned char)T[I + 1] == 0xBB && (unsigned char)T[I + 2] == 0xBF) { I += 3; continue; }
			return;
		}
	}
	bool Fail(const char* Why) { if (Error.empty()) Error = std::string(Why) + " at offset " + std::to_string(I); return false; }

	static void AppendUtf8(std::string& S, uint32_t C)
	{
		if (C < 0x80) S.push_back((char)C);
		else if (C < 0x800) { S.push_back((char)(0xC0 | C >> 6)); S.push_back((char)(0x80 | (C & 63))); }
		else if (C < 0x10000) { S.push_back((char)(0xE0 | C >> 12)); S.push_back((char)(0x80 | (C >> 6 & 63))); S.push_back((char)(0x80 | (C & 63))); }
		else { S.push_back((char)(0xF0 | C >> 18)); S.push_back((char)(0x80 | (C >> 12 & 63))); S.push_back((char)(0x80 | (C >> 6 & 63))); S.push_back((char)(0x80 | (C & 63))); }
	}

	bool String(std::string& Out)
	{
		if (T[I] != '"') return Fail("expected string");
		I++;
		while (I < T.size() && T[I] != '"')
		{
			char c = T[I++];
			if (c != '\\') { Out.push_back(c); continue; }
			if (I >= T.size()) return Fail("bad escape");
			char e = T[I++];
			switch (e)
			{
			case 'n': Out.push_back('\n'); break; case 't': Out.push_back('\t'); break; case 'r': Out.push_back('\r'); break;
			case 'b': Out.push_back('\b'); break; case 'f': Out.push_back('\f'); break;
			case 'u':
			{
				if (I + 4 > T.size()) return Fail("bad \\u");
				uint32_t Code = (uint32_t)std::stoul(T.substr(I, 4), nullptr, 16); I += 4;
				AppendUtf8(Out, Code); break;
			}
			default: Out.push_back(e);
			}
		}
		if (I >= T.size()) return Fail("unterminated string");
		I++;
		return true;
	}

	bool Value(FJson& Out)
	{
		Skip();
		if (I >= T.size()) return Fail("unexpected end");
		char c = T[I];
		if (c == '{')
		{
			Out.Type = FJson::Object; I++;
			for (;;)
			{
				Skip();
				if (I < T.size() && T[I] == '}') { I++; return true; }
				std::string Key;
				if (I >= T.size() || !String(Key)) return Fail("expected key");
				Skip();
				if (I >= T.size() || T[I] != ':') return Fail("expected ':'");
				I++;
				Out.Members.emplace_back(Key, FJson());
				if (!Value(Out.Members.back().second)) return false;
				Skip();
				if (I < T.size() && T[I] == ',') { I++; continue; }
				if (I < T.size() && T[I] == '}') { I++; return true; }
				return Fail("expected ',' or '}'");
			}
		}
		if (c == '[')
		{
			Out.Type = FJson::Array; I++;
			for (;;)
			{
				Skip();
				if (I < T.size() && T[I] == ']') { I++; return true; }
				Out.Items.emplace_back();
				if (!Value(Out.Items.back())) return false;
				Skip();
				if (I < T.size() && T[I] == ',') { I++; continue; }
				if (I < T.size() && T[I] == ']') { I++; return true; }
				return Fail("expected ',' or ']'");
			}
		}
		if (c == '"') { Out.Type = FJson::String; return String(Out.Str); }
		if (T.compare(I, 4, "true") == 0) { Out.Type = FJson::Bool; Out.B = true; I += 4; return true; }
		if (T.compare(I, 5, "false") == 0) { Out.Type = FJson::Bool; Out.B = false; I += 5; return true; }
		if (T.compare(I, 4, "null") == 0) { Out.Type = FJson::Null; I += 4; return true; }
		size_t Start = I;
		while (I < T.size() && (std::isdigit((unsigned char)T[I]) || T[I] == '-' || T[I] == '+' || T[I] == '.' || T[I] == 'e' || T[I] == 'E')) I++;
		if (I == Start) return Fail("unexpected character");
		double V = 0;
		auto R = std::from_chars(T.data() + Start, T.data() + I, V);
		if (R.ec != std::errc()) return Fail("bad number");
		Out.Type = FJson::Number; Out.Num = V;
		return true;
	}
};

bool EqualsNoCase(const std::string& A, const char* B)
{
	size_t n = std::strlen(B);
	if (A.size() != n) return false;
	for (size_t i = 0; i < n; i++) if (std::tolower((unsigned char)A[i]) != std::tolower((unsigned char)B[i])) return false;
	return true;
}
}

bool FJson::Parse(const std::string& Text, FJson& Out, std::string& Error)
{
	FJsonReader R(Text);
	if (!R.Value(Out)) { Error = R.Error; return false; }
	return true;
}

const FJson* FJson::Find(const char* Name) const
{
	// (System.Text.Json keeps the last of duplicate members)
	const FJson* Found = nullptr;
	for (const auto& m : Members) if (EqualsNoCase(m.first, Name)) Found = &m.second;
	return Found && Found->Type != Null ? Found : nullptr;
}
double FJson::NumberOr(const char* Name, double Default) const { const FJson* v = Find(Name); return v && v->Type == Number ? v->Num : Default; }
std::string FJson::StringOr(const char* Name, const std::string& Default) const { const FJson* v = Find(Name); return v && v->Type == String ? v->Str : Default; }
bool FJson::BoolOr(const char* Name, bool Default) const { const FJson* v = Find(Name); return v && v->Type == Bool ? v->B : Default; }

// ------------------------------------------------------------------------------------------------ profile
static bool InRange(double x, double lo, double hi) { return std::isfinite(x) && x >= lo && x <= hi; }

bool FWorldProfile::Validate(std::string& Error) const
{
	bool IdOk = !Id.empty() && Id.size() <= 64;
	for (char c : Id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) IdOk = false;
	if (!IdOk) { Error = "Invalid world profile id."; return false; }
	if (Name.empty() || Name.size() > 160) { Error = "Invalid world profile name."; return false; }
	if (!InRange(WorldSize, 16000, 4096000) || Continents < 1 || Continents > 16 || Islands < 0 || Islands > 400 ||
		!InRange(ContinentRadius, WorldSize * .035, WorldSize * .28) || !InRange(IslandRadius, WorldSize * .002, WorldSize * .025) ||
		!InRange(CoastRoughness, 0, .3) || !InRange(SeaLevel, -1000, 1000) || !InRange(OceanDepth, 50, 8000) ||
		!InRange(LowlandHeight, 10, 800) || !InRange(HillHeight, 0, 800) || !InRange(HillScale, 500, WorldSize * .1) ||
		RangesPerContinent < 0 || RangesPerContinent > 6 || !InRange(MountainHeight, 0, 6000) ||
		!InRange(MountainWidth, 500, ContinentRadius * .5))
	{
		Error = "Unsupported world profile dimensions."; return false;
	}
	return true;
}

// System.Text.Json's default string escaping: everything outside its allowed Basic Latin set is \uXXXX.
static void AppendJsonString(std::string& Out, const std::string& S)
{
	static const char* Hex = "0123456789ABCDEF";
	auto Unicode = [&](uint32_t C) { Out += "\\u"; for (int s = 12; s >= 0; s -= 4) Out.push_back(Hex[(C >> s) & 15]); };
	Out.push_back('"');
	for (size_t i = 0; i < S.size();)
	{
		unsigned char c = (unsigned char)S[i];
		uint32_t Code; int Len;
		if (c < 0x80) { Code = c; Len = 1; }
		else if ((c & 0xE0) == 0xC0) { Code = c & 0x1F; Len = 2; }
		else if ((c & 0xF0) == 0xE0) { Code = c & 0x0F; Len = 3; }
		else { Code = c & 0x07; Len = 4; }
		for (int k = 1; k < Len && i + k < S.size(); k++) Code = Code << 6 | ((unsigned char)S[i + k] & 0x3F);
		i += Len;
		if (Code == '\\') Out += "\\\\";
		else if (Code == '\n') Out += "\\n";
		else if (Code == '\r') Out += "\\r";
		else if (Code == '\t') Out += "\\t";
		else if (Code == '\b') Out += "\\b";
		else if (Code == '\f') Out += "\\f";
		else if (Code < 0x20 || Code == '"' || Code == '&' || Code == '\'' || Code == '+' || Code == '<' || Code == '>' || Code == '`' || Code == 0x7F) Unicode(Code);
		else if (Code < 0x80) Out.push_back((char)Code);
		else if (Code < 0x10000) Unicode(Code);
		else { Code -= 0x10000; Unicode(0xD800 + (Code >> 10)); Unicode(0xDC00 + (Code & 0x3FF)); }
	}
	Out.push_back('"');
}

// Shortest round-trip decimal, as .NET writes doubles in JSON (plain notation in the profile's value ranges).
static void AppendJsonNumber(std::string& Out, double V)
{
	char Buffer[64];
	auto R = std::to_chars(Buffer, Buffer + sizeof Buffer, V, std::chars_format::fixed);
	Out.append(Buffer, R.ptr);
}

std::string FWorldProfile::CanonicalJson() const
{
	std::string J = "{\"Id\":";
	AppendJsonString(J, Id);
	J += ",\"Name\":"; AppendJsonString(J, Name);
	auto D = [&](const char* Key, double V) { J += ",\""; J += Key; J += "\":"; AppendJsonNumber(J, V); };
	auto N = [&](const char* Key, int V) { J += ",\""; J += Key; J += "\":"; J += std::to_string(V); };
	D("WorldSize", WorldSize); N("Continents", Continents); N("Islands", Islands);
	D("ContinentRadius", ContinentRadius); D("IslandRadius", IslandRadius); D("CoastRoughness", CoastRoughness);
	D("SeaLevel", SeaLevel); D("OceanDepth", OceanDepth); D("LowlandHeight", LowlandHeight); D("HillHeight", HillHeight);
	D("HillScale", HillScale); N("RangesPerContinent", RangesPerContinent); D("MountainHeight", MountainHeight); D("MountainWidth", MountainWidth);
	J += "}";
	return J;
}

std::string FWorldProfile::Digest() const { return FSha256::HexOf(CanonicalJson()); }

bool FWorldProfile::ParseCatalog(const std::string& Json, std::vector<FWorldProfile>& Out, std::string& Error)
{
	FJson Root;
	if (!FJson::Parse(Json, Root, Error)) return false;
	if (Root.Type != FJson::Array || Root.Items.empty() || Root.Items.size() > 64) { Error = "Invalid profile catalogue."; return false; }
	for (const FJson& o : Root.Items)
	{
		FWorldProfile p;
		p.Id = o.StringOr("Id", ""); p.Name = o.StringOr("Name", "");
		p.WorldSize = o.NumberOr("WorldSize", 0); p.Continents = (int)o.NumberOr("Continents", 0); p.Islands = (int)o.NumberOr("Islands", 0);
		p.ContinentRadius = o.NumberOr("ContinentRadius", 0); p.IslandRadius = o.NumberOr("IslandRadius", 0);
		p.CoastRoughness = o.NumberOr("CoastRoughness", 0); p.SeaLevel = o.NumberOr("SeaLevel", 0); p.OceanDepth = o.NumberOr("OceanDepth", 0);
		p.LowlandHeight = o.NumberOr("LowlandHeight", 0); p.HillHeight = o.NumberOr("HillHeight", 0); p.HillScale = o.NumberOr("HillScale", 0);
		p.RangesPerContinent = (int)o.NumberOr("RangesPerContinent", 0); p.MountainHeight = o.NumberOr("MountainHeight", 0);
		p.MountainWidth = o.NumberOr("MountainWidth", 0);
		if (!p.Validate(Error)) return false;
		Out.push_back(p);
	}
	return true;
}

double FClimateSettings::Span(double WorldSize) const
{
	return LatitudeSpan > 0 ? LatitudeSpan : DetMath::Clamp(6 + 64 * std::sqrt(WorldSize / 4096000), 6, 70);
}

FWorldManifest FWorldManifest::Create(int Seed, const FWorldProfile& Profile, double OriginX, double OriginZ)
{
	FWorldManifest m;
	m.Seed = Seed; m.OriginX = OriginX; m.OriginZ = OriginZ; m.Profile = Profile; m.ProfileDigest = Profile.Digest();
	return m;
}

// ------------------------------------------------------------------------------------------------ recipes
const std::vector<double>& FWorldRecipe::Sizes()
{
	static const std::vector<double> S = { 32, 64, 128, 256, 512, 768, 1024, 2048, 4096 };
	return S;
}

std::vector<FWorldRecipe> FWorldRecipe::Catalog(const std::vector<FWorldProfile>& Profiles)
{
	auto P = [&](const char* Id) -> const FWorldProfile* { for (const auto& p : Profiles) if (p.Id == Id) return &p; return nullptr; };
	std::vector<FWorldRecipe> List;
	auto Add = [&](const char* Id, const char* Name, const char* Description, const FWorldProfile* Base, bool Mainland)
	{
		if (!Base) return;
		FWorldRecipe r; r.Id = Id; r.Name = Name; r.Description = Description; r.Base = *Base; r.bMainland = Mainland;
		List.push_back(r);
	};
	const FWorldProfile* Balanced = P("balanced");
	Add("balanced", "Balanced continents", "Three-ish large continents, moderate islands and coasts, two mountain systems each.", Balanced, false);
	Add("continental", "Great mainland", "One dominant landmass with a few peripheral islands.", P("continental"), true);
	Add("archipelago", "Archipelago", "Many small and medium landmasses, many islands, lots of coast.", P("archipelago"), false);
	Add("mountainous", "Mountain kingdoms", "Continents dominated by high mountain ranges.", P("mountainous"), false);
	if (Balanced)
	{
		FWorldProfile Many = *Balanced;
		Many.Id = "many-continents"; Many.Name = "Many small continents"; Many.Continents = 8; Many.Islands = 40; Many.ContinentRadius = 48000;
		Many.IslandRadius = 6000; Many.CoastRoughness = .25; Many.RangesPerContinent = 1; Many.MountainWidth = 7000;
		Add("many-continents", "Many small continents", "Eight or more medium landmasses separated by narrow seas.", &Many, false);
		FWorldProfile Isles = *Balanced;
		Isles.Id = "islands-mainland"; Isles.Name = "Islands + mainland"; Isles.Continents = 1; Isles.Islands = 90; Isles.ContinentRadius = 115000;
		Isles.IslandRadius = 7000; Isles.CoastRoughness = .24; Isles.RangesPerContinent = 3; Isles.MountainWidth = 11000;
		Add("islands-mainland", "Islands + mainland", "One large mainland surrounded by many islands and island chains.", &Isles, true);
	}
	Add("vast-world", "Vast continents (classic)", "The original 2,048 km 'Vast world' preset.", P("vast-world"), false);
	return List;
}

static std::string WholeNumber(double V) { return std::to_string((i64)std::floor(V + .5)); }

FWorldProfile FWorldRecipe::Resolve(const FWorldChoices& c) const
{
	const FWorldProfile& b = Base;
	double size = c.SizeKm > 0 ? c.SizeKm * 1000 : b.WorldSize;
	bool autoAll = c.Continents < 0 && c.Islands < 0 && c.Ranges < 0 && c.ContinentSize == 1 && c.CoastRoughness == 1
		&& c.MountainHeight == 1 && c.MountainWidth == 1 && c.Hills == 1;
	if (autoAll && std::fabs(size - b.WorldSize) < 1) return b; // native size, all Auto: exactly the preset
	double f = size / b.WorldSize;

	int continents = c.Continents >= 0 ? c.Continents : (bMainland || b.Continents == 0 ? b.Continents : (int)std::nearbyint(b.Continents * std::pow(f, .8)));
	continents = ClampI(continents, 1, 16);
	double radius = b.ContinentRadius * f * (continents > 0 && b.Continents > 0 ? std::sqrt(b.Continents / (double)continents) : 1) * c.ContinentSize;
	radius = DetMath::Clamp(radius, size * .036, size * .279);
	int islands = c.Islands >= 0 ? c.Islands : (int)std::nearbyint(b.Islands * std::pow(f, .8));
	islands = ClampI(islands, 0, 400);
	double islandRadius = b.IslandRadius * f * std::sqrt(MxI(1, b.Islands) / (double)MxI(1, islands));
	islandRadius = DetMath::Clamp(islandRadius, size * .0021, size * .0249);

	double widthScale = radius / b.ContinentRadius;
	bool real = c.ScaleRevision >= 9;
	double mountainWidth = DetMath::Clamp(b.MountainWidth * std::pow(widthScale, real ? .35 : .7) * c.MountainWidth, 501, radius * .499);
	double mountainHeight = DetMath::Clamp(b.MountainHeight * c.MountainHeight * DetMath::Clamp(std::sqrt(mountainWidth / b.MountainWidth), real ? .7 : .35, 1), 0, 6000);
	double hillScale = real ? DetMath::Clamp(b.HillScale * std::pow(f, .3), Mn(4000, size * .099), size * .099)
		: DetMath::Clamp(b.HillScale * std::pow(f, .6), 501, size * .099);
	double hillHeight = DetMath::Clamp(b.HillHeight * c.Hills * (real ? DetMath::Clamp(std::pow(f, .15), .7, 1) : DetMath::Clamp(std::pow(f, .25), .5, 1)), 0, 800);
	double oceanDepth = DetMath::Clamp(b.OceanDepth * DetMath::Clamp(std::pow(f, .3), .3, 1), 50, 8000);

	FWorldProfile p = b;
	p.Id = Id + "-" + WholeNumber(size / 1000) + "km";
	p.Name = Name + " (" + WholeNumber(size / 1000) + " km)";
	p.WorldSize = size; p.Continents = continents; p.Islands = islands; p.ContinentRadius = radius; p.IslandRadius = islandRadius;
	p.CoastRoughness = DetMath::Clamp(b.CoastRoughness * c.CoastRoughness, 0, .3);
	p.RangesPerContinent = ClampI(c.Ranges >= 0 ? c.Ranges : b.RangesPerContinent, 0, 6);
	p.MountainHeight = mountainHeight; p.MountainWidth = mountainWidth; p.HillHeight = hillHeight; p.HillScale = hillScale; p.OceanDepth = oceanDepth;
	return p;
}

// ------------------------------------------------------------------------------------------------ layout
static FBeltKnot Spline(const std::vector<FBeltKnot>& points, int i, double t, double width, double height)
{
	const FBeltKnot& a = points[MxI(0, i - 1)]; const FBeltKnot& b = points[i]; const FBeltKnot& c = points[i + 1];
	const FBeltKnot& d = points[MnI((int)points.size() - 1, i + 2)];
	auto Cubic = [t](double p0, double p1, double p2, double p3)
	{
		return .5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
	};
	double smooth = DetMath::Smooth(t);
	FBeltKnot k;
	k.Point.X = Cubic(a.Point.X, b.Point.X, c.Point.X, d.Point.X); k.Point.Z = Cubic(a.Point.Z, b.Point.Z, c.Point.Z, d.Point.Z);
	k.Width = (b.Width + (c.Width - b.Width) * smooth) * width; k.Height = (b.Height + (c.Height - b.Height) * smooth) * height;
	k.U = b.U + (c.U - b.U) * t;
	return k;
}

static std::vector<FBeltKnot> Controls(FPoint2 center, double angle, double length, double curvature, double phase, int count, FRandomStream& rng)
{
	std::vector<double> steps(count - 1);
	for (double& s : steps) s = rng.Between(.6, 1.5);
	double total = 0;
	for (double s : steps) total += s;
	double distance = 0, c = DetMath::Cos(angle), s = DetMath::Sin(angle);
	std::vector<FBeltKnot> result;
	for (int i = 0; i < count; i++)
	{
		double u = distance / total, along = (u - .5) * length;
		double across = curvature * (DetMath::Sin(u * DetMath::Pi * 1.4 + phase) + .28 * DetMath::Sin(u * DetMath::Pi * 3.2 - phase));
		FBeltKnot k;
		k.Point.X = center.X + c * along - s * across; k.Point.Z = center.Z + s * along + c * across;
		k.Width = rng.Between(.55, 1.5); k.Height = rng.Between(.6, 1); k.U = u;
		result.push_back(k);
		if (i < (int)steps.size()) distance += steps[i];
	}
	return result;
}

static FBeltKnot Interpolate(const std::vector<FBeltKnot>& points, double u)
{
	int i = 0;
	while (i + 2 < (int)points.size() && points[i + 1].U < u) i++;
	return Spline(points, i, (u - points[i].U) / (points[i + 1].U - points[i].U), 1, 1);
}

FWorldLayout::FWorldLayout(const FWorldManifest& InManifest) : Manifest(InManifest)
{
	const FWorldProfile& p = Manifest.Profile;
	auto Seed3 = [&](const char* layer, int ordinal) { return Hashing::FeatureSeed(Manifest.Seed, 3, Manifest.ProfileDigest, layer, 0, 0, ordinal); };
	ReliefNoise = Seed3("relief", 0);
	for (int i = 0; i < p.Continents + p.Islands; i++)
	{
		bool island = i >= p.Continents;
		FRandomStream random(Seed3(island ? "island-shape" : "continent-shape", i));
		double size = (island ? p.IslandRadius : p.ContinentRadius) * (.8 + random.Unit() * .2);
		double x = 0, z = 0;
		if (!island)
		{
			double limit = p.WorldSize * .46 - size * 1.3, best = -Inf;
			for (int attempt = 0; attempt < 160; attempt++)
			{
				double cx = (random.Unit() * 2 - 1) * limit, cz = (random.Unit() * 2 - 1) * limit;
				double clearance = Inf;
				for (const FLandFeature& other : Land)
				{
					double dx = cx - (other.Center.X - Manifest.OriginX), dz = cz - (other.Center.Z - Manifest.OriginZ);
					clearance = Mn(clearance, std::sqrt(dx * dx + dz * dz) - (size + other.RadiusX) * 1.15);
				}
				if (clearance > best) { best = clearance; x = cx; z = cz; }
				if (clearance >= 0) break;
			}
		}
		else
		{
			int groups = MxI(1, MnI(12, p.Islands / 10));
			FRandomStream group(Seed3("island-group", (i - p.Continents) % groups));
			double gx = (group.Unit() - .5) * p.WorldSize * .76, gz = (group.Unit() - .5) * p.WorldSize * .76, angle = group.Unit() * DetMath::TwoPi;
			double along = (random.Unit() - .5) * p.IslandRadius * 12, across = (random.Unit() - .5) * p.IslandRadius * 4;
			double c = DetMath::Cos(angle), s = DetMath::Sin(angle);
			x = DetMath::Clamp(gx + c * along - s * across, -p.WorldSize * .43, p.WorldSize * .43);
			z = DetMath::Clamp(gz + s * along + c * across, -p.WorldSize * .43, p.WorldSize * .43);
		}
		FLandFeature f;
		f.Center.X = x + Manifest.OriginX; f.Center.Z = z + Manifest.OriginZ;
		f.Id = Hashing::FeatureId(Manifest.Seed, 3, Manifest.ProfileDigest, island ? "island" : "continent",
			(i64)std::floor(f.Center.X / 16), (i64)std::floor(f.Center.Z / 16), i);
		f.bIsland = island; f.RadiusX = size;
		f.RadiusZ = size * (.6 + random.Unit() * .3);
		f.Angle = random.Unit() * DetMath::Pi;
		f.NoiseSeed = random.Next();
		f.Cos = DetMath::Cos(f.Angle); f.Sin = DetMath::Sin(f.Angle); f.LobePhase = Hashing::Unit(f.NoiseSeed) * DetMath::TwoPi;
		Land.push_back(f);
	}
	BuildBelts();
}

void FWorldLayout::BuildBelts()
{
	const FWorldProfile& p = Manifest.Profile;
	auto Seed4 = [&](const char* layer, int ordinal) { return Hashing::FeatureSeed(Manifest.Seed, 4, Manifest.ProfileDigest, layer, 0, 0, ordinal); };
	MountainNoise = Seed4("mountain-field", 0);
	if (p.RangesPerContinent <= 0 || p.MountainHeight <= 0) return;
	for (int continent = 0; continent < p.Continents; continent++)
	{
		const FLandFeature feature = Land[continent];
		FRandomStream rng(Seed4("mountain-shape", continent));

		auto Add = [&](const std::vector<FBeltKnot>& controls, double baseWidth, double amplitude, bool segmented, double taper)
		{
			FMountainBelt belt;
			for (int i = 0; i < (int)controls.size() - 1; i++)
				for (int j = 0; j < 4; j++) belt.Points.push_back(Spline(controls, i, j / 4.0, baseWidth, amplitude));
			FBeltKnot last = controls.back();
			last.Width = controls.back().Width * baseWidth; last.Height = controls.back().Height * amplitude;
			belt.Points.push_back(last);
			belt.Passes.resize(1 + rng.Int(3));
			for (int i = 0; i < (int)belt.Passes.size(); i++)
			{
				double u = rng.Between(.2, .8), span = rng.Between(.025, .075);
				double depth = segmented && i == 0 ? .99 : rng.Between(.25, .65);
				belt.Passes[i] = { u, span, depth };
			}
			double maxWidth = -Inf, minX = Inf, minZ = Inf, maxX = -Inf, maxZ = -Inf;
			for (const FBeltKnot& k : belt.Points)
			{
				maxWidth = Mx(maxWidth, k.Width); minX = Mn(minX, k.Point.X); minZ = Mn(minZ, k.Point.Z); maxX = Mx(maxX, k.Point.X); maxZ = Mx(maxZ, k.Point.Z);
			}
			double extent = maxWidth * 3 + p.MountainWidth;
			belt.Id = Hashing::FeatureId(Manifest.Seed, 4, Manifest.ProfileDigest, "range",
				(i64)std::floor(feature.Center.X / 16), (i64)std::floor(feature.Center.Z / 16), (int)Belts.size());
			belt.Continent = continent; belt.Taper = taper;
			belt.MinX = minX - extent; belt.MinZ = minZ - extent; belt.MaxX = maxX + extent; belt.MaxZ = maxZ + extent;
			belt.BaseWidth = baseWidth; belt.Amplitude = amplitude * p.MountainHeight;
			Belts.push_back(std::move(belt));
		};

		FPoint2 center{ feature.Center.X - Manifest.OriginX, feature.Center.Z - Manifest.OriginZ };
		double angle = feature.Angle + rng.Between(-.25, .25), style = rng.Unit();
		bool broken = style >= .55 && style < .75, massif = style >= .9;
		double length = feature.RadiusX * (massif ? rng.Between(.55, .85) : rng.Between(1.1, 1.6));
		double curvature = feature.RadiusZ * (style < .18 ? rng.Between(.025, .065) : rng.Between(.12, .36));
		double width = p.MountainWidth * (massif ? rng.Between(1.7, 2.5) : rng.Between(1.0, 1.65));
		double phase = rng.Unit() * DetMath::TwoPi;
		std::vector<FBeltKnot> main = Controls(center, angle, length, curvature, phase, 7, rng);
		double mainAmplitude = rng.Between(.75, 1), mainTaper = rng.Between(.13, .25);
		Add(main, width, mainAmplitude, broken, mainTaper);
		int auxiliary = rng.Int(p.RangesPerContinent);
		for (int branch = 0; branch < auxiliary; branch++)
		{
			double position = rng.Between(.16, .84);
			FBeltKnot start = Interpolate(main, position);
			bool parallel = branch == 0 && style >= .75 && style < .9;
			double side = rng.Unit() < .5 ? -1 : 1;
			double branchAngle = angle + side * rng.Between(.45, 1.8);
			double branchLength = feature.RadiusZ * rng.Between(.3, .85);
			std::vector<FBeltKnot> points;
			if (parallel)
			{
				double offsetScale = rng.Between(1.5, 2.5);
				double offset = width * offsetScale * (rng.Unit() < .5 ? -1 : 1);
				double sa = DetMath::Sin(angle), ca = DetMath::Cos(angle);
				points = main;
				for (FBeltKnot& k : points) { double px = k.Point.X - sa * offset, pz = k.Point.Z + ca * offset; k.Point.X = px; k.Point.Z = pz; }
			}
			else
			{
				FPoint2 branchCenter{ start.Point.X + DetMath::Cos(branchAngle) * branchLength * .5, start.Point.Z + DetMath::Sin(branchAngle) * branchLength * .5 };
				double branchCurvature = branchLength * rng.Between(.08, .22);
				double branchPhase = rng.Unit() * DetMath::TwoPi;
				points = Controls(branchCenter, branchAngle, branchLength, branchCurvature, branchPhase, 5, rng);
				FPoint2 shift{ start.Point.X - points[0].Point.X, start.Point.Z - points[0].Point.Z };
				for (FBeltKnot& k : points) { k.Point.X = k.Point.X + shift.X; k.Point.Z = k.Point.Z + shift.Z; }
			}
			double bw = width * rng.Between(.4, .85), amp = rng.Between(.35, .7);
			bool seg = rng.Unit() < .2;
			double taper = rng.Between(.16, .3);
			Add(points, bw, amp, seg, taper);
		}
	}
}

double FWorldLayout::LandInfluence(double x, double z) const
{
	const FWorldProfile& p = Manifest.Profile;
	double lx = x - Manifest.OriginX, lz = z - Manifest.OriginZ;
	double edge = (p.WorldSize * .5 - Mx(std::fabs(lx), std::fabs(lz))) / (p.WorldSize * .06);
	if (edge <= 0) return Mn(-1, edge - .3);
	double land = -1;
	for (const FLandFeature& f : Land)
	{
		double dx = x - f.Center.X, dz = z - f.Center.Z;
		if (std::fabs(dx) > f.RadiusX * 3 || std::fabs(dz) > f.RadiusX * 3) continue;
		double scale = f.RadiusX, cr = p.CoastRoughness;
		double wx = dx + Noise::Value(dx / (scale * .7), dz / (scale * .7), f.NoiseSeed) * scale * cr * .5;
		double wz = dz + Noise::Value(dx / (scale * .7) + 31.7, dz / (scale * .7) - 18.3, f.NoiseSeed) * scale * cr * .5;
		double a = (f.Cos * wx + f.Sin * wz) / f.RadiusX;
		double b = (-f.Sin * wx + f.Cos * wz) / f.RadiusZ;
		double coast = (Noise::Value(dx / (scale * .28), dz / (scale * .28), f.NoiseSeed) * .7 +
			Noise::Value(dx / (scale * .09), dz / (scale * .09), f.NoiseSeed + 1) * .3) * cr;
		double radius = std::sqrt(a * a + b * b), theta = DetMath::Atan2(b, a), phase = f.LobePhase;
		double lobes = cr * (.9 * DetMath::Sin(theta * 2 + phase) + .65 * DetMath::Sin(theta * 3 - phase) + .4 * DetMath::Sin(theta * 5 + phase * .7));
		double influence = 1 - radius + coast + lobes * DetMath::Smooth(radius / .6) * (f.bIsland ? .55 : 1);
		if (influence > land) land = influence;
	}
	return Mn(land, edge - .3);
}

void FWorldLayout::MountainWarp(double lx, double lz, double& wx, double& wz) const
{
	double w = Manifest.Profile.MountainWidth;
	u64 n = MountainNoise;
	wx = lx + w * (.42 * Noise::Value(lx / (w * 2.1), lz / (w * 2.1), n) + .16 * Noise::Value(lx / (w * .63) + 17, lz / (w * .63), n + 1));
	wz = lz + w * (.42 * Noise::Value(lx / (w * 2.1) - 31, lz / (w * 2.1) + 9, n + 2) + .16 * Noise::Value(lx / (w * .63), lz / (w * .63) - 13, n + 3));
}

double FWorldLayout::RuggedBroad(double wx, double wz) const
{
	double w = Manifest.Profile.MountainWidth;
	u64 n = MountainNoise;
	double nx = wx * .857 - wz * .515, nz = wx * .515 + wz * .857;
	double n1 = 1 - std::fabs(Noise::Value(nx / (w * 1.1), nz / (w * 1.1), n + 4));
	double n2 = 1 - std::fabs(Noise::Value((nx + nz) / (w * .59), (nz - nx) / (w * .59), n + 5));
	return n1 * n1 * (.45 + .55 * n2 * n2);
}

void FWorldLayout::MountainEnvelope(double lx, double lz, double& core, double& shoulder) const
{
	core = 0; shoulder = 0;
	if (Belts.empty()) return;
	double profileWidth = Manifest.Profile.MountainWidth;
	double wx, wz;
	MountainWarp(lx, lz, wx, wz);
	double distances[MaximumSegmentsPerBelt], projections[MaximumSegmentsPerBelt];
	for (const FMountainBelt& belt : Belts)
	{
		if (lx < belt.MinX || lx > belt.MaxX || lz < belt.MinZ || lz > belt.MaxZ) continue;
		double nearest = Inf;
		const std::vector<FBeltKnot>& pts = belt.Points;
		for (int i = 1; i < (int)pts.size(); i++)
		{
			const FBeltKnot& a = pts[i - 1]; const FBeltKnot& b = pts[i];
			double dx = b.Point.X - a.Point.X, dz = b.Point.Z - a.Point.Z;
			double t = DetMath::Clamp(((wx - a.Point.X) * dx + (wz - a.Point.Z) * dz) / (dx * dx + dz * dz), 0, 1);
			double ex = wx - a.Point.X - dx * t, ez = wz - a.Point.Z - dz * t, distance = ex * ex + ez * ez;
			projections[i - 1] = t; distances[i - 1] = distance;
			if (distance < nearest) nearest = distance;
		}
		double width = 0, amplitude = 0, along = 0, weightSum = 0;
		double blend = profileWidth * profileWidth * .04;
		for (int i = 1; i < (int)pts.size(); i++)
		{
			double w = DetMath::Smooth(1 - (distances[i - 1] - nearest) / blend);
			if (w <= 0) continue;
			const FBeltKnot& a = pts[i - 1]; const FBeltKnot& b = pts[i]; double t = projections[i - 1];
			width += (a.Width + (b.Width - a.Width) * t) * w;
			amplitude += (a.Height + (b.Height - a.Height) * t) * w;
			along += (a.U + (b.U - a.U) * t) * w;
			weightSum += w;
		}
		width /= weightSum; amplitude /= weightSum; along /= weightSum;
		double taper = DetMath::Smooth(along / belt.Taper) * DetMath::Smooth((1 - along) / belt.Taper);
		width *= .4 + .6 * taper;
		double r2 = nearest / (width * width);
		if (r2 >= 9 || taper <= 0) continue;
		double saddle = 1;
		for (const FBeltPass& pass : belt.Passes)
		{
			double q = (along - pass.U) / pass.Span;
			saddle *= 1 - pass.Depth * DetMath::Exp(-q * q);
		}
		double k = amplitude * taper * saddle * DetMath::Smooth(9 - r2);
		double c = Mn(1, DetMath::Exp(-r2 * 3.2) * k), s = Mn(1, DetMath::Exp(-r2 * .9) * k);
		core += (1 - core) * c;
		shoulder += (1 - shoulder) * s;
	}
}
}
