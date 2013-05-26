/**
\param canvas is the name of the HTML canvas element to draw on
\param data is an array of objects. Each object is a data point with a
   'time' field indicating the position on the x-axis and then
   arbitrary additional fields for the y-axis for all the graphs
\param graphs is an array of objects specifying metadata about each graph
   to plot. The most important field is 'name' which indicates the
   name of the field in the data array to use for the plot. Also specify
   'color' which is a string defining the color of the line (CSS style).
\param start_time is a number indicating the left-most position on the
   x-axis (the time-axis). This number is arbitrary, but it must be less than
   'now' and it must be of the same unit as the numbers in the 'time' fields
   in 'data'.
\param now is a number similar to start_time, but represents the right-most
   point on the x-axis. It represents the time value for the latest sample.
\param unit this is an optional unit of the values on the y-axis, and will
   be printed in the right margin for each of the tics.
\param scale this is an optional scale factor for the data. The unit will
   automatically have an SI prefix added to it based on the scale. Specifying
   this locks the prefix to a specific one regardless of the magnitude of
   the data points. This is primarily useful to make a graph always specify
   transfer rates in kB/s, in which case it should be set to 1000.
\param multiplier this is an optional number that's multiplied to all y-values
   before used by any part of the graphing logic. This can be used to turn a
   scaled unit into the base SI unit, to make the applied prefixes apply correctly.
   For instance, time values specified in microseconds could have a multiplier
   of 0.000001 in order to turn them into seconds
\param use_legend this is an optional boolean defaulting to false. When true,
   a legend of the graphs is rendered in the top left corner. In this case,
   each object in the graphs array may have a 'label' field indicating the
   name for that graph in the legend. If no label is specified, the 'name'
   field is used.
*/

function render_graph(canvas, data, graphs, start_time, now, unit, scale, multiplier, use_legend)
{
	if (typeof(unit) == 'undefined') unit = '';
	if (typeof(scale) == 'undefined') scale = 1;
	if (typeof(multiplier) == 'undefined') multiplier = 1;
	if (typeof(use_legend) == 'undefined') use_legend = false;

	var canvas = document.getElementById(canvas);
	var ctx = canvas.getContext('2d');
	
	// first find the highest rate, in order to scale
	var peak = 0;
	for (dp in data)
	{
		for (g in graphs)
		{
			var n = graphs[g].name;
			if (!data[dp].hasOwnProperty(n)) continue;
			peak = Math.max(data[dp][n] * multiplier, peak);
		}
	}
   if (peak == 0) peak = 1 * (scale == 'auto' ? 1 : scale);

	ctx.clearRect(0,0,canvas.width, canvas.height);
	ctx.save();

	var window_size = now - start_time;

	var view_width = canvas.width - 40;
	var view_height = canvas.height - 5;

	// the 0.5 is to align lines with pixels
	ctx.translate(0.5, 4.5);

	// used for text
	ctx.fillStyle = "black";
	ctx.lineWidth = 1;

	// calculate the number of tics and the peak
	var num_tics = 10;
	var new_peak = 1;
	while (peak > new_peak)
		new_peak *= 10;

	num_tics = Math.ceil(peak / new_peak * 10);
	peak = new_peak * num_tics / 10;
	if (num_tics < 5) num_tics *= 2;

	if (scale == 'auto')
	{
		if (peak >= 1000000000)
		{
			scale = 1000000000;
		}
		else if (peak >= 1000000)
		{
			scale = 1000000;
		}
		else if (peak >= 1000)
		{
			scale = 1000;
		}
		else if (peak >= 1)
		{
			scale = 1;
		}
		else if (peak >= 0.001)
		{
			scale = 1 / 1000;
		}
		else if (peak >= 0.000001)
		{
			scale = 1 / 1000000;
		}
		else if (peak >= 0.000000001)
		{
			scale = 1 / 1000000000;
		}
	}

	if (scale >= 1000000000)
	{
		unit = 'G' + unit;
	}
	else if (scale >= 1000000)
	{
		unit = 'M' + unit;
	}
	else if (scale >= 1000)
	{
		unit = 'k' + unit;
	}
   else if (scale >= 1)
   {
      // do nothing
   }
   else if (scale >= 0.001)
   {
      unit = 'm' + unit;
   }
   else if (scale >= 0.00001)
   {
      unit = 'u' + unit;
   }
   else if (scale >= 0.000000001)
   {
      unit = 'n' + unit;
   }

	var scalex = view_width / (now - start_time);
	var scaley = view_height / peak;

	// draw y-axis tics
	for (var i = 0; i < num_tics; ++i)
	{
		ctx.strokeStyle = "#000";
		ctx.setLineDash([]);
		var y = Math.floor(view_height * i / num_tics);
		ctx.beginPath();
		ctx.moveTo(view_width - 6, y);
		ctx.lineTo(view_width, y);
		ctx.lineTo(view_width, y + view_height / num_tics);
		ctx.stroke();
		var rate = peak - peak * i / num_tics;
		ctx.fillText((rate / scale).toFixed( peak < 5*scale ? 1 : 0) + ' ' + unit, view_width + 2, y + 4);

		ctx.setLineDash([5]);
		ctx.strokeStyle = "#ccc";
		ctx.beginPath();
		ctx.moveTo(view_width - 6, y);
		ctx.lineTo(0, y);
		ctx.stroke();
	}

	ctx.setLineDash([]);

	// plot all the graphs
	for (k in graphs)
	{
		var g = graphs[k];

		ctx.strokeStyle = g.color;
		var first = true;
		ctx.beginPath();
		for (i in data)
		{
			var time = data[i].time;
			var y = data[i][g.name] * multiplier;
			if (typeof(y) == 'undefined') continue;
   
			if (first)
			{
				ctx.moveTo((time - start_time) * scalex, view_height - y * scaley);
				first = false;
			}
			else
			{
				ctx.lineTo((time - start_time) * scalex, view_height - y * scaley);
			}
		}
		ctx.stroke();
	}

	if (use_legend)
	{
		ctx.font = 'normal 12pt Calibri';
		var legend_width = 0;
		for (k in graphs)
		{
			g = graphs[k];
			var label;
			if (g.label) label = g.label;
			else label = g.name;
			legend_width = Math.max(legend_width, ctx.measureText(label).width);
		}

		var offset = 15;
		ctx.fillStyle = 'rgba(255,255,255,0.7)';
		ctx.strokeStyle = 'black';
		ctx.fillRect(5, offset - 8, 23 + legend_width, graphs.length * 16 + 1);
		ctx.strokeRect(4, offset - 9, 24 + legend_width, graphs.length * 16 + 2);
		ctx.fillStyle = 'black';

		ctx.lineWidth = 2;

		for (k in graphs)
		{
			g = graphs[k];
			var label;
			if (g.label) label = g.label;
			else label = g.name;

			ctx.strokeStyle = g.color;
			ctx.beginPath();
			ctx.moveTo(7, offset);
			ctx.lineTo(20, offset);
			ctx.stroke();

			ctx.fillText(label, 23, offset + 4);
			offset += 16;
		}
	}
	ctx.restore();
}

window['render_graph'] = render_graph;

