using System.Collections;
using Terminal.Gui;

return EnvCli.Run(args);

static class EnvCli
{
	public static int Run(string[] args)
	{
		if (args.Length == 0)
		{
            HandleHelp();
			return 0;
		}

		var command = args[0].ToLowerInvariant();
		var tail = args.Skip(1).ToArray();

		try
		{
			return command switch
			{
				"get" => HandleGet(tail),
				"set" => HandleSet(tail),
				"unset" => HandleUnset(tail),
				"list" => HandleList(tail),
				"tui" => HandleTui(tail),
				"help" or "--help" or "-h" => HandleHelp(),
				_ => Fail($"Unknown command '{command}'.")
			};
		}
		catch (Exception ex)
		{
			Console.Error.WriteLine($"Error: {ex.Message}");
			return 1;
		}
	}

	private static int HandleGet(string[] args)
	{
		if (args.Length == 0)
		{
			return Fail("Usage: env get <name> [--scope local|global|all]");
		}

		var name = args[0];
		var options = ParseOptions(args.Skip(1).ToArray(), Scope.All, allowAllScope: true, allowForceGlobal: false);
		var scope = options.Scope;

		return scope switch
		{
			Scope.Local => PrintSingle(name, EnvironmentVariableTarget.User, "local", Scope.Local),
			Scope.Global => PrintSingle(name, EnvironmentVariableTarget.Machine, "global", Scope.Global),
			Scope.All => PrintAll(name),
			_ => 1
		};
	}

	private static int HandleSet(string[] args)
	{
		if (args.Length < 2)
		{
			return Fail("Usage: env set <name> <value> [--scope local|global]");
		}

		var name = args[0];
		var value = args[1];
		var options = ParseOptions(args.Skip(2).ToArray(), Scope.Local, allowAllScope: false, allowForceGlobal: true);
		var scope = options.Scope;

		if (scope == Scope.All)
		{
			return Fail("'set' only supports --scope local|global.");
		}

		if (scope == Scope.Global && !options.ForceGlobal)
		{
			return Fail("Refusing to write global variable without --force-global.");
		}

		var target = ToTarget(scope);
		try
		{
			Environment.SetEnvironmentVariable(name, value, target);
		}
		catch (UnauthorizedAccessException)
		{
			return Fail("Access denied writing global variables. Run terminal as Administrator.");
		}
		catch (Exception ex) when (scope == Scope.Global)
		{
			return Fail($"Global write failed: {ex.Message}. Run terminal as Administrator.");
		}
		Console.WriteLine($"Set {ScopeName(scope)} variable '{name}'.");
		return 0;
	}

	private static int HandleUnset(string[] args)
	{
		if (args.Length == 0)
		{
			return Fail("Usage: env unset <name> [--scope local|global]");
		}

		var name = args[0];
		var options = ParseOptions(args.Skip(1).ToArray(), Scope.Local, allowAllScope: false, allowForceGlobal: true);
		var scope = options.Scope;

		if (scope == Scope.All)
		{
			return Fail("'unset' only supports --scope local|global.");
		}

		if (scope == Scope.Global && !options.ForceGlobal)
		{
			return Fail("Refusing to remove global variable without --force-global.");
		}

		var target = ToTarget(scope);
		try
		{
			Environment.SetEnvironmentVariable(name, null, target);
		}
		catch (UnauthorizedAccessException)
		{
			return Fail("Access denied writing global variables. Run terminal as Administrator.");
		}
		catch (Exception ex) when (scope == Scope.Global)
		{
			return Fail($"Global write failed: {ex.Message}. Run terminal as Administrator.");
		}
		Console.WriteLine($"Unset {ScopeName(scope)} variable '{name}'.");
		return 0;
	}

	private static int HandleList(string[] args)
	{
		var options = ParseOptions(args, Scope.All, allowAllScope: true, allowForceGlobal: false);
		var scope = options.Scope;
		var local = ReadVariables(EnvironmentVariableTarget.User);
		var global = ReadVariables(EnvironmentVariableTarget.Machine);

		var names = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
		if (scope is Scope.Local or Scope.All)
		{
			foreach (var key in local.Keys)
			{
				names.Add(key);
			}
		}

		if (scope is Scope.Global or Scope.All)
		{
			foreach (var key in global.Keys)
			{
				names.Add(key);
			}
		}

		foreach (var name in names)
		{
			switch (scope)
			{
				case Scope.Local:
					Console.WriteLine($"{name}={local.GetValueOrDefault(name, string.Empty)}");
					break;
				case Scope.Global:
					Console.WriteLine($"{name}={global.GetValueOrDefault(name, string.Empty)}");
					break;
				case Scope.All:
					var localVal = local.GetValueOrDefault(name, "-");
					var globalVal = global.GetValueOrDefault(name, "-");
					Console.WriteLine($"{name} | local={localVal} | global={globalVal}");
					break;
			}
		}

		return 0;
	}

	private static int HandleTui(string[] args)
	{
		if (args.Length > 0)
		{
			return Fail("Usage: env tui");
		}

		RunTui();
		return 0;
	}

	private static int HandleHelp()
	{
		Console.WriteLine("env - environment variable manager");
		Console.WriteLine();
		Console.WriteLine("Commands:");
		Console.WriteLine("  env get <name> [--scope local|global|all]");
		Console.WriteLine("  env set <name> <value> [--scope local|global] [--force-global]");
		Console.WriteLine("  env unset <name> [--scope local|global] [--force-global]");
		Console.WriteLine("  env list [--scope local|global|all]");
		Console.WriteLine("  env tui");
		Console.WriteLine();
		Console.WriteLine("Scopes:");
		Console.WriteLine("  local  = current user environment variables");
		Console.WriteLine("  global = machine-wide environment variables (admin rights may be required)");
		Console.WriteLine("  all    = read both local and global");
		Console.WriteLine();
		Console.WriteLine("Safety:");
		Console.WriteLine("  global writes via set/unset require --force-global");
		return 0;
	}

	private static int PrintSingle(string name, EnvironmentVariableTarget target, string label, Scope scope)
	{
		var value = Environment.GetEnvironmentVariable(name, target);
		if (value is null)
		{
			Console.WriteLine($"{name} not found in {label} scope.");
			PrintSuggestions(name, scope);
			return 1;
		}

		Console.WriteLine(value);
		return 0;
	}

	private static int PrintAll(string name)
	{
		var local = Environment.GetEnvironmentVariable(name, EnvironmentVariableTarget.User);
		var global = Environment.GetEnvironmentVariable(name, EnvironmentVariableTarget.Machine);

		if (local is null && global is null)
		{
			Console.WriteLine($"{name} not found in local or global scope.");
			PrintSuggestions(name, Scope.All);
			return 1;
		}

		Console.WriteLine($"local:  {local ?? "-"}");
		Console.WriteLine($"global: {global ?? "-"}");
		return 0;
	}

	private static Dictionary<string, string> ReadVariables(EnvironmentVariableTarget target)
	{
		try
		{
			var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			var values = Environment.GetEnvironmentVariables(target);

			foreach (DictionaryEntry entry in values)
			{
				if (entry.Key is string key)
				{
					result[key] = entry.Value?.ToString() ?? string.Empty;
				}
			}

			return result;
		}
		catch
		{
			return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		}
	}

	private static CliOptions ParseOptions(string[] args, Scope defaultScope, bool allowAllScope, bool allowForceGlobal)
	{
		var scope = defaultScope;
		var forceGlobal = false;

		for (var i = 0; i < args.Length; i++)
		{
			var arg = args[i];

			if (arg.Equals("--scope", StringComparison.OrdinalIgnoreCase))
			{
				i++;
				if (i >= args.Length)
				{
					throw new InvalidOperationException("Missing value for --scope.");
				}

				scope = ParseScopeValue(args[i], allowAllScope);
				continue;
			}

			if (arg.Equals("--force-global", StringComparison.OrdinalIgnoreCase))
			{
				if (!allowForceGlobal)
				{
					throw new InvalidOperationException("--force-global is not valid for this command.");
				}

				forceGlobal = true;
				continue;
			}

			throw new InvalidOperationException($"Unknown option '{arg}'.");
		}

		return new CliOptions(scope, forceGlobal);
	}

	private static Scope ParseScopeValue(string value, bool allowAllScope)
	{
		var scope = value.ToLowerInvariant() switch
		{
			"local" => Scope.Local,
			"global" => Scope.Global,
			"all" => Scope.All,
			_ => throw new InvalidOperationException($"Unknown scope '{value}'.")
		};

		if (!allowAllScope && scope == Scope.All)
		{
			throw new InvalidOperationException("This command does not support --scope all.");
		}

		return scope;
	}

	private static void PrintSuggestions(string input, Scope scope)
	{
		var suggestions = SuggestNames(input, scope);
		if (suggestions.Count == 0)
		{
			return;
		}

		Console.WriteLine("Did you mean:");
		foreach (var suggestion in suggestions)
		{
			Console.WriteLine($"  {suggestion}");
		}
	}

	private static List<string> SuggestNames(string input, Scope scope)
	{
		var local = ReadVariables(EnvironmentVariableTarget.User);
		var global = ReadVariables(EnvironmentVariableTarget.Machine);
		var allNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		if (scope is Scope.Local or Scope.All)
		{
			foreach (var name in local.Keys)
			{
				allNames.Add(name);
			}
		}

		if (scope is Scope.Global or Scope.All)
		{
			foreach (var name in global.Keys)
			{
				allNames.Add(name);
			}
		}

		return allNames
			.Select(name => new
			{
				Name = name,
				Match = SuggestionScore(input, name)
			})
			.Where(x => x.Match.IsRelevant)
			.OrderBy(x => x.Match.Score)
			.ThenBy(x => x.Name, StringComparer.OrdinalIgnoreCase)
			.Take(8)
			.Select(x => x.Name)
			.ToList();
	}

	private static SuggestionMatch SuggestionScore(string input, string candidate)
	{
		if (candidate.Equals(input, StringComparison.OrdinalIgnoreCase))
		{
			return new SuggestionMatch(0, true);
		}

		if (candidate.StartsWith(input, StringComparison.OrdinalIgnoreCase))
		{
			return new SuggestionMatch(1, true);
		}

		if (candidate.Contains(input, StringComparison.OrdinalIgnoreCase))
		{
			return new SuggestionMatch(2, true);
		}

		var distance = Levenshtein(input.ToUpperInvariant(), candidate.ToUpperInvariant());
		return new SuggestionMatch(10 + distance, distance <= 3);
	}

	private static int Levenshtein(string a, string b)
	{
		var d = new int[a.Length + 1, b.Length + 1];

		for (var i = 0; i <= a.Length; i++)
		{
			d[i, 0] = i;
		}

		for (var j = 0; j <= b.Length; j++)
		{
			d[0, j] = j;
		}

		for (var i = 1; i <= a.Length; i++)
		{
			for (var j = 1; j <= b.Length; j++)
			{
				var cost = a[i - 1] == b[j - 1] ? 0 : 1;
				d[i, j] = Math.Min(
					Math.Min(d[i - 1, j] + 1, d[i, j - 1] + 1),
					d[i - 1, j - 1] + cost
				);
			}
		}

		return d[a.Length, b.Length];
	}

	private static EnvironmentVariableTarget ToTarget(Scope scope) => scope switch
	{
		Scope.Local => EnvironmentVariableTarget.User,
		Scope.Global => EnvironmentVariableTarget.Machine,
		_ => throw new InvalidOperationException("Only local and global can be used for set/unset.")
	};

	private static string ScopeName(Scope scope) => scope == Scope.Local ? "local" : "global";

	private static int Fail(string message)
	{
		Console.Error.WriteLine(message);
		return 1;
	}

	private static void RunTui()
	{
		Application.Init();

		var top = Application.Top;
		var win = new Window("env editor")
		{
			X = 0,
			Y = 0,
			Width = Dim.Fill(),
			Height = Dim.Fill()
		};

		var scopeRadio = new RadioGroup(["Local", "Global", "All"])
		{
			X = 1,
			Y = 1,
			Width = 24,
			Height = 4,
			SelectedItem = 2
		};

		var list = new ListView(Array.Empty<string>())
		{
			X = 1,
			Y = Pos.Bottom(scopeRadio) + 1,
			Width = 35,
			Height = Dim.Fill(3)
		};

		var nameLabel = new Label("Name:")
		{
			X = Pos.Right(list) + 2,
			Y = Pos.Top(list)
		};

		var nameField = new TextField("")
		{
			X = Pos.Left(nameLabel),
			Y = Pos.Bottom(nameLabel),
			Width = Dim.Fill(2)
		};

		var valueLabel = new Label("Value:")
		{
			X = Pos.Left(nameLabel),
			Y = Pos.Bottom(nameField) + 1
		};

		var valueField = new TextView()
		{
			X = Pos.Left(valueLabel),
			Y = Pos.Bottom(valueLabel),
			Width = Dim.Fill(2),
			Height = 3
		};

		var segmentsLabel = new Label("Segments:")
		{
			X = Pos.Left(valueLabel),
			Y = Pos.Bottom(valueField) + 1
		};

		var segmentList = new ListView(Array.Empty<string>())
		{
			X = Pos.Left(segmentsLabel),
			Y = Pos.Bottom(segmentsLabel),
			Width = Dim.Fill(2),
			Height = 6
		};

		var segmentEditorLabel = new Label("Selected Segment:")
		{
			X = Pos.Left(segmentsLabel),
			Y = Pos.Bottom(segmentList) + 1
		};

		var segmentEditor = new TextField("")
		{
			X = Pos.Left(segmentEditorLabel),
			Y = Pos.Bottom(segmentEditorLabel),
			Width = Dim.Fill(2)
		};

		var newButton = new Button("New")
		{
			X = Pos.Left(nameLabel),
			Y = Pos.Bottom(segmentEditor) + 1
		};

		var applySegmentButton = new Button("Apply Segment")
		{
			X = Pos.Right(newButton) + 1,
			Y = Pos.Top(newButton)
		};

		var addSegmentButton = new Button("Add Segment")
		{
			X = Pos.Right(applySegmentButton) + 1,
			Y = Pos.Top(newButton)
		};

		var removeSegmentButton = new Button("Remove Segment")
		{
			X = Pos.Right(addSegmentButton) + 1,
			Y = Pos.Top(newButton)
		};

		var moveUpButton = new Button("Up")
		{
			X = Pos.Left(nameLabel),
			Y = Pos.Bottom(newButton) + 1
		};

		var moveDownButton = new Button("Down")
		{
			X = Pos.Right(moveUpButton) + 1,
			Y = Pos.Top(moveUpButton)
		};

		var saveButton = new Button("Save")
		{
			X = Pos.Right(moveDownButton) + 1,
			Y = Pos.Top(moveUpButton)
		};

		var deleteButton = new Button("Delete")
		{
			X = Pos.Right(saveButton) + 1,
			Y = Pos.Top(moveUpButton)
		};

		var refreshButton = new Button("Refresh")
		{
			X = Pos.Right(deleteButton) + 1,
			Y = Pos.Top(moveUpButton)
		};

		var quitButton = new Button("Quit")
		{
			X = Pos.AnchorEnd(10),
			Y = Pos.Top(moveUpButton)
		};

		var rows = new List<EnvRow>();
		var segments = new List<string>();

		void RefreshSegmentList(int selectedIndex = 0)
		{
			if (segments.Count == 0)
			{
				segments.Add(string.Empty);
			}

			segmentList.SetSource(segments.Select(FormatSegment).ToList());
			segmentList.SelectedItem = Math.Clamp(selectedIndex, 0, segments.Count - 1);
			segmentEditor.Text = segments[segmentList.SelectedItem];
		}

		void Reload()
		{
			rows.Clear();
			rows.AddRange(BuildRows((Scope)scopeRadio.SelectedItem));
			list.SetSource(rows.Select(x => x.Display).ToList());
			if (rows.Count == 0)
			{
				nameField.Text = string.Empty;
				valueField.Text = string.Empty;
				segments.Clear();
				segmentList.SetSource(Array.Empty<string>());
				segmentEditor.Text = string.Empty;
				return;
			}

			list.SelectedItem = 0;
			FillEditor(rows[0]);
		}

		void FillEditor(EnvRow row)
		{
			nameField.Text = row.Name;
			valueField.Text = row.EditValue;
			segments.Clear();
			segments.AddRange(SplitSegments(row.EditValue));
			RefreshSegmentList();
		}

		list.SelectedItemChanged += e =>
		{
			var index = e.Item;
			if (index >= 0 && index < rows.Count)
			{
				FillEditor(rows[index]);
			}
		};

		segmentList.SelectedItemChanged += e =>
		{
			if (e.Item >= 0 && e.Item < segments.Count)
			{
				segmentEditor.Text = segments[e.Item];
			}
		};

		scopeRadio.SelectedItemChanged += _ => Reload();

		newButton.Clicked += () =>
		{
			nameField.Text = string.Empty;
			valueField.Text = string.Empty;
			segments.Clear();
			RefreshSegmentList();
			nameField.SetFocus();
		};

		applySegmentButton.Clicked += () =>
		{
			if (segments.Count == 0)
			{
				segments.Add(string.Empty);
			}

			var index = segmentList.SelectedItem;
			if (index < 0 || index >= segments.Count)
			{
				index = 0;
			}

			segments[index] = segmentEditor.Text?.ToString() ?? string.Empty;
			RefreshSegmentList(index);
			valueField.Text = string.Join(";", segments);
		};

		addSegmentButton.Clicked += () =>
		{
			var index = segmentList.SelectedItem;
			if (index < 0 || index >= segments.Count)
			{
				index = segments.Count - 1;
			}

			segments.Insert(index + 1, string.Empty);
			RefreshSegmentList(index + 1);
			valueField.Text = string.Join(";", segments);
		};

		removeSegmentButton.Clicked += () =>
		{
			if (segments.Count <= 1)
			{
				segments.Clear();
				segments.Add(string.Empty);
				RefreshSegmentList(0);
				valueField.Text = string.Join(";", segments);
				return;
			}

			var index = segmentList.SelectedItem;
			if (index < 0 || index >= segments.Count)
			{
				index = segments.Count - 1;
			}

			segments.RemoveAt(index);
			RefreshSegmentList(Math.Min(index, segments.Count - 1));
			valueField.Text = string.Join(";", segments);
		};

		moveUpButton.Clicked += () => MoveSegment(-1);
		moveDownButton.Clicked += () => MoveSegment(1);

		void MoveSegment(int delta)
		{
			if (segments.Count <= 1)
			{
				return;
			}

			var index = segmentList.SelectedItem;
			if (index < 0 || index >= segments.Count)
			{
				return;
			}

			var newIndex = index + delta;
			if (newIndex < 0 || newIndex >= segments.Count)
			{
				return;
			}

			(segments[index], segments[newIndex]) = (segments[newIndex], segments[index]);
			RefreshSegmentList(newIndex);
			valueField.Text = string.Join(";", segments);
		}

		saveButton.Clicked += () =>
		{
			var name = nameField.Text?.ToString()?.Trim() ?? string.Empty;
			var value = string.Join(";", segments);

			if (string.IsNullOrWhiteSpace(name))
			{
				MessageBox.ErrorQuery("Invalid", "Name cannot be empty.", "OK");
				return;
			}

			var targetScope = (Scope)scopeRadio.SelectedItem;
			if (targetScope == Scope.All)
			{
				targetScope = Scope.Local;
			}

			try
			{
				Environment.SetEnvironmentVariable(name, value, ToTarget(targetScope));
				Reload();
			}
			catch (Exception ex)
			{
				MessageBox.ErrorQuery("Save Failed", ex.Message, "OK");
			}
		};

		deleteButton.Clicked += () =>
		{
			var name = nameField.Text?.ToString()?.Trim() ?? string.Empty;
			if (string.IsNullOrWhiteSpace(name))
			{
				return;
			}

			var targetScope = (Scope)scopeRadio.SelectedItem;
			if (targetScope == Scope.All)
			{
				targetScope = Scope.Local;
			}

			var answer = MessageBox.Query("Confirm", $"Delete variable '{name}' from {ScopeName(targetScope)}?", "Yes", "No");
			if (answer != 0)
			{
				return;
			}

			try
			{
				Environment.SetEnvironmentVariable(name, null, ToTarget(targetScope));
				Reload();
			}
			catch (Exception ex)
			{
				MessageBox.ErrorQuery("Delete Failed", ex.Message, "OK");
			}
		};

		refreshButton.Clicked += Reload;
		quitButton.Clicked += () => Application.RequestStop(win);

		win.Add(scopeRadio);
		win.Add(list);
		win.Add(nameLabel, nameField, valueLabel, valueField, segmentsLabel, segmentList, segmentEditorLabel, segmentEditor);
		win.Add(newButton, applySegmentButton, addSegmentButton, removeSegmentButton, moveUpButton, moveDownButton, saveButton, deleteButton, refreshButton, quitButton);
		top.Add(win);

		Reload();
		Application.Run();
		Application.Shutdown();
	}

	private static List<EnvRow> BuildRows(Scope scope)
	{
		var local = ReadVariables(EnvironmentVariableTarget.User);
		var global = ReadVariables(EnvironmentVariableTarget.Machine);
		var names = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);

		if (scope is Scope.Local or Scope.All)
		{
			foreach (var name in local.Keys)
			{
				names.Add(name);
			}
		}

		if (scope is Scope.Global or Scope.All)
		{
			foreach (var name in global.Keys)
			{
				names.Add(name);
			}
		}

		var rows = new List<EnvRow>();
		foreach (var name in names)
		{
			var localVal = local.GetValueOrDefault(name, string.Empty);
			var globalVal = global.GetValueOrDefault(name, string.Empty);

			rows.Add(scope switch
			{
				Scope.Local => new EnvRow(name, localVal, $"{name} = {localVal}"),
				Scope.Global => new EnvRow(name, globalVal, $"{name} = {globalVal}"),
				Scope.All => new EnvRow(name, localVal.Length > 0 ? localVal : globalVal, $"{name} | L={localVal} | G={globalVal}"),
				_ => new EnvRow(name, string.Empty, name)
			});
		}

		return rows;
	}

	private static List<string> SplitSegments(string value)
	{
		if (string.IsNullOrEmpty(value))
		{
			return [string.Empty];
		}

		return value
			.Split(';')
			.ToList();
	}

	private static string FormatSegment(string segment) => string.IsNullOrEmpty(segment) ? "(empty)" : segment;

	private readonly record struct EnvRow(string Name, string EditValue, string Display);

	private readonly record struct CliOptions(Scope Scope, bool ForceGlobal);

	private readonly record struct SuggestionMatch(int Score, bool IsRelevant);

	private enum Scope
	{
		Local,
		Global,
		All
	}
}
